// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/list_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/ui_scope_descriptor_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/list_layout_preparer.h>
#include <impl/ecs_ui/toolkit/widgets/list_input.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ListResult ListCoordinator::virtualList(const AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options){
    ListResult result;
    if(m_builder.itemDeclarationBlocked() || !IsFinite(options.wheelRows) || options.wheelRows <= 0.0f){
        m_builder.m_context.fail();
        return result;
    }
    // A body callback may have closed or reopened its popup; no model loan is allowed past that boundary.
    if(!m_builder.synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_builder.m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_builder.m_declaring = false; });
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::VirtualList);
    if(!widget)
        return result;
    const b8 previouslyFocused = m_builder.m_context.input().focus() == (*widget)->id;
    const u64 previousSelection = state.selectedKey();
    if(!ListBehavior::Reconcile(state, source)){
        m_builder.m_context.fail();
        return result;
    }
    const ControlToken token{ state.inputGeneration(), source.instanceGeneration(), source.revision() };
    m_builder.m_context.input().fenceControl((*widget)->id, (*widget)->declarationGeneration, token);
    if(!options.enabled)
        m_builder.m_context.input().invalidateTarget((*widget)->id);
    result.valid = true;
    result.selectionChanged = previousSelection != state.selectedKey();
    result.focused = options.enabled && m_builder.m_context.input().focus() == (*widget)->id;
    if(!ListInput(m_builder.m_context).consume(
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
        m_builder.m_context.fail();
        result.valid = false;
        return result;
    }
    ListFrame frame(m_builder.m_arena);
    const UiSkinRegion* background = prepareListFrame(source, state, options, token, source.rowCount(), options.enabled && previouslyFocused && !result.focused, {}, frame);
    if(!background){
        m_builder.m_context.fail();
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
            const auto placement = ListLayoutPreparer(m_builder).measureListPlacement(frame, provisional, provisional.clip);
            if(!placement){
                m_builder.m_context.fail();
                result.valid = false;
                return result;
            }
            const f32 height = ResolveListIntrinsicSize(*background, frame.padding, frame.rowHeight).y;
            if(height == placement->bounds.height)
                break;
        }
    }
    Item item(m_builder.m_arena);
    item.state = **widget;
    item.enabled = options.enabled;
    item.list = static_cast<u32>(m_builder.m_scope->m_lists.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ResolveListIntrinsicSize(*background, frame.padding, frame.rowHeight);
    if(!m_builder.addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    m_builder.m_scope->m_lists.push_back(Move(frame));
    m_builder.m_scope->m_items.push_back(Move(item));
    return result;
}

const UiSkinRegion* ListCoordinator::prepareListFrame(
    const IListDataSource& source, ListState& state, const ListOptions& options, const ControlToken& token,
    const u64 rowCount, const b8 focusOnCommit, const WidgetId commandFocus, ListFrame& outFrame
){
    outFrame.source = &source;
    outFrame.state = &state;
    outFrame.widgetStyle = m_builder.m_style;
    outFrame.style = m_builder.m_listStyle;
    outFrame.options = options;
    outFrame.token = token;
    outFrame.rowCount = rowCount;
    outFrame.focusOnCommit = focusOnCommit;
    outFrame.commandFocus = commandFocus;
    outFrame.padding = outFrame.style.padding;
    const UiSkinRegion* background = m_builder.region(outFrame.style.background, outFrame.style.backgroundFallback);
    if(!background)
        return nullptr;
    outFrame.padding = MaxUiPadding(outFrame.padding, background->padding);
    return prepareListMetrics(outFrame) ? background : nullptr;
}

b8 ListCoordinator::prepareListMetrics(ListFrame& frame){
    if(!m_builder.listMatches(frame) || !m_builder.m_skin)
        return false;
    const auto metrics = MeasureSelectable(*m_builder.m_skin, frame.style.row);
    if(!metrics)
        return false;
    frame.style.row.padding = metrics->padding;
    const auto keyResult = TryBuildListMeasureKey(
        frame.token.contentGeneration,
        frame.token.contentRevision,
        frame.rowCount,
        m_builder.m_text.identity(),
        m_builder.m_text.generation(),
        frame.widgetStyle.fontSize,
        m_builder.m_paint.displayMetrics(),
        *metrics,
        m_builder.m_textScriptTag,
        StringView(m_builder.m_textLanguage.data(), m_builder.m_textLanguage.size())
    );
    if(!keyResult)
        return false;
    const ListMeasureKey key = *keyResult;
    frame.measureKey = key;
    ListState& state = *frame.state;
    if(key != state.m_measureKey || state.m_rowHeight <= 0.0f){
        auto empty = m_builder.m_text.layout(m_builder.textShapeRequest({}, key.fontSize));
        if(!empty)
            return false;
        const auto bounds = m_builder.m_text.measure(*empty, m_builder.m_paint.displayMetrics());
        if(!bounds)
            return false;
        const f32 height = ResolveListRowBaseHeight(*metrics, bounds->height);
        if(!IsFinite(height) || height <= 0.0f || !m_builder.listMatches(frame))
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
    return m_builder.listMatches(frame);
}

ListResult Builder::virtualList(const AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options){
    return ListCoordinator(*this).virtualList(stableKey, source, state, options);
}

const UiSkinRegion* Builder::prepareListFrame(const IListDataSource& source, ListState& state, const ListOptions& options, const ControlToken& token, const u64 rowCount, const b8 focusOnCommit, const WidgetId commandFocus, ListFrame& outFrame){
    return ListCoordinator(*this).prepareListFrame(source, state, options, token, rowCount, focusOnCommit, commandFocus, outFrame);
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


Expected<ListLayoutPreparer::ListRow> ListLayoutPreparer::prepareListRow(const ListFrame& frame, const u64 index){
    ListRow row(m_builder.m_arena);
    if(!m_builder.listMatches(frame) || index >= frame.rowCount)
        return MakeUnexpected(Failure{});
    const u64 key = frame.source->key(index);
    if(key == 0u || !m_builder.listStateMatches(frame))
        return MakeUnexpected(Failure{});
    const auto resolved = frame.source->indexOf(key);
    if(!resolved || *resolved != index || !m_builder.listStateMatches(frame))
        return MakeUnexpected(Failure{});
    const b8 enabled = frame.options.enabled && frame.source->enabled(index);
    if(!m_builder.listStateMatches(frame))
        return MakeUnexpected(Failure{});
    const ShapeRequest request{ frame.source->text(index), StringView(reinterpret_cast<const char*>(frame.measureKey.language.data())), frame.widgetStyle.fontSize, frame.measureKey.scriptTag, TextDirection::LeftToRight };
    if(!m_builder.listStateMatches(frame))
        return MakeUnexpected(Failure{});
    // The source owns a temporary view; shaping must copy it before any further source callback.
    auto layout = m_builder.m_text.layout(request);
    if(!layout || !m_builder.listMatches(frame))
        return MakeUnexpected(Failure{});
    const auto bounds = m_builder.m_text.measure(*layout, m_builder.m_paint.displayMetrics());
    if(!bounds)
        return MakeUnexpected(Failure{});
    row.index = index;
    row.key = key;
    row.enabled = enabled;
    row.textBounds = *bounds;
    row.text = Move(*layout);
    if(!m_builder.listMatches(frame))
        return MakeUnexpected(Failure{});
    return row;
}

Expected<ScrollPlacement> ListLayoutPreparer::measureListPlacement(ListFrame& frame, const LayoutBox& box, const Rect& clip){
    auto& rows = frame.rows;
    ListState& state = *frame.state;
    ScrollState scroll;
    if(!m_builder.listMatches(frame) || !scroll.setOffset(frame.scrollOffset))
        return MakeUnexpected(Failure{});
    const b8 revealCursor = frame.revealCursor && frame.cursorKey != 0u;
    u64 cursorIndex = 0u;
    if(revealCursor){
        const auto resolved = frame.source->indexOf(frame.cursorKey);
        if(!resolved || *resolved >= frame.rowCount || !m_builder.listStateMatches(frame))
            return MakeUnexpected(Failure{});
        cursorIndex = *resolved;
        if(frame.source->key(cursorIndex) != frame.cursorKey || !m_builder.listStateMatches(frame) || !frame.source->enabled(cursorIndex) || !m_builder.listMatches(frame))
            return MakeUnexpected(Failure{});
    }
    for(;;){
        auto candidate = CalculateScrollPlacement(box.rectangle, clip, frame.padding, frame.style.scrollbarWidth, frame.style.minimumThumb, frame.rowCount, frame.rowHeight, scroll.offset());
        if(!candidate)
            return MakeUnexpected(Failure{});
        if(revealCursor){
            const f64 start = static_cast<f64>(cursorIndex) * static_cast<f64>(frame.rowHeight);
            const f64 end = start + static_cast<f64>(frame.rowHeight);
            if(!IsFinite(start) || !IsFinite(end) || end <= start || !scroll.ensureVisible(start, end, candidate->viewport.height))
                return MakeUnexpected(Failure{});
        }
        if(revealCursor)
            candidate = CalculateScrollPlacement(box.rectangle, clip, frame.padding, frame.style.scrollbarWidth, frame.style.minimumThumb, frame.rowCount, frame.rowHeight, scroll.offset());
        if(!candidate || candidate->endRow - candidate->firstRow > s_ListMaxPreparedRows)
            return MakeUnexpected(Failure{});
        f32 height = frame.rowHeight;
        for(u64 index = candidate->firstRow; index < candidate->endRow; ++index){
            const auto found = LowerBound(rows.begin(), rows.end(), index, [](const ListRow& row, const u64 value)noexcept{ return row.index < value; });
            if(found != rows.end() && found->index == index)
                continue;
            // The work bound is independent of dataset size, even when a cursor reveal introduces a new interval.
            if(rows.size() >= s_ListMaxPreparedRows)
                return MakeUnexpected(Failure{});
            auto row = prepareListRow(frame, index);
            if(!row)
                return MakeUnexpected(Failure{});
            height = ComputeListRowPaddedHeight(row->textBounds.height, frame.style.row.padding, height);
            rows.insert(found, Move(*row));
        }
        if(!IsFinite(height) || height <= 0.0f || !m_builder.listMatches(frame))
            return MakeUnexpected(Failure{});
        if(height == frame.rowHeight){
            frame.scrollOffset = candidate->offset;
            // Another declaration can share this state with a different sizing context.
            if(state.m_measureKey == frame.measureKey)
                state.m_rowHeight = Max(state.m_rowHeight, frame.rowHeight);
            return *candidate;
        }
        // Retain the first visible row and its pixel offset at the inherited clip edge while the uniform estimate grows.
        const f64 offset = ComputeListExpandedScrollOffset(
            candidate->contentClip.y,
            candidate->viewport.y,
            candidate->offset,
            candidate->firstRow,
            frame.rowHeight,
            height
        );
        if(!scroll.setOffset(offset))
            return MakeUnexpected(Failure{});
        frame.rowHeight = height;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ListCoordinator::paintList(const Item& item, const LayoutBox& box){
    if(item.list >= m_builder.m_scope->m_lists.size())
        return false;
    ListFrame& frame = m_builder.m_scope->m_lists[item.list];
    if(!m_builder.listMatches(frame))
        return false;
    const Rect clip = m_builder.visibleClip(box.clip);
    const auto measuredPlacement = ListLayoutPreparer(m_builder).measureListPlacement(frame, box, clip);
    if(!measuredPlacement)
        return false;
    const ScrollPlacement& placement = *measuredPlacement;
    if(!frame.state->m_scroll.setOffset(frame.scrollOffset))
        return false;
    frame.state->m_scrollRowHeight = frame.rowHeight;
    frame.state->m_scrollSourceGeneration = frame.token.contentGeneration;
    if(frame.revealCursor && frame.state->cursorKey() == frame.cursorKey)
        frame.state->m_ensureCursor = false;
    frame.state->m_placement = placement;
    const UiSkinRegion* background = m_builder.region(frame.style.background, frame.style.backgroundFallback);
    if(!background)
        return false;
    m_builder.m_paint.pushClip(clip);
    const b8 painted = m_builder.m_paint.drawRegion(background->name, box.rectangle);
    const b8 popped = m_builder.m_paint.popClip();
    if(!painted || !popped)
        return false;
    const HitTarget host = HitTargetFactory().makeListHostHitTarget(
        box.rectangle,
        clip,
        item.enabled,
        item.contextMenu,
        frame.token,
        frame.rowHeight,
        frame.options.wheelRows,
        placement,
        frame.rowCount,
        frame.focusOnCommit
    );
    if(!m_builder.m_context.addTarget(item.state, host) || !paintListRows(item, frame, placement))
        return false;
    // Paint owns its glyph pages now; keep no shaped rows alive through later sibling lists.
    frame.rows.clear();
    if(placement.scrollbarVisible){
        const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollbar);
        if(!thumbId)
            return false;
        const b8 hover = m_builder.m_context.input().hover() == *thumbId;
        if(!ControlVisualPainter().paintListScrollbarVisual(m_builder.m_paint, *m_builder.m_skin, frame.style, placement, clip, hover))
            return false;
        if(item.enabled){
            const HitTarget trackTarget = HitTargetFactory().makeListScrollbarTrackHitTarget(placement, clip, frame.token);
            const auto trackId = TryMakeWidgetId(item.state.id, s_WidgetPartTrack);
            if(!trackId || !m_builder.m_context.addPartTarget(item.state, *trackId, trackTarget))
                return false;
            const HitTarget thumbTarget = HitTargetFactory().makeListScrollbarThumbHitTarget(placement, clip, frame.token);
            if(!m_builder.m_context.addPartTarget(item.state, *thumbId, thumbTarget))
                return false;
        }
    }
    return true;
}

b8 ListCoordinator::paintListRows(const Item& item, const ListFrame& frame, const ScrollPlacement& placement){
    const auto& rows = frame.rows;
    const auto rowsId = TryMakeWidgetId(item.state.id, s_WidgetPartRows);
    if(!rowsId)
        return false;
    const b8 focused = m_builder.m_context.input().focus() == item.state.id || (frame.commandFocus.valid() && m_builder.m_context.input().focus() == frame.commandFocus);
    auto found = LowerBound(rows.begin(), rows.end(), placement.firstRow, [](const ListRow& row, const u64 value)noexcept{ return row.index < value; });
    for(u64 index = placement.firstRow; index < placement.endRow; ++index, ++found){
        if(found == rows.end() || found->index != index || !m_builder.listStateMatches(frame))
            return false;
        const ListRow& row = *found;
        const u64 key = row.key;
        const b8 enabled = item.enabled && row.enabled;
        const auto part = TryMakeWidgetPartId(*rowsId, key);
        if(!part)
            return false;
        const auto rectangle = CalculateScrollRowBounds(index, placement, frame.rowHeight);
        if(!rectangle)
            return false;
        const SelectablePaintFlags flags{ enabled, frame.state->selectedKey() == key, m_builder.m_context.input().hover() == *part, focused && frame.state->cursorKey() == key };
        if(!PaintSelectable(m_builder.m_paint, m_builder.m_text, *m_builder.m_skin, row.text, row.textBounds, frame.style.row.padding, *rectangle, placement.contentClip, frame.style.row, flags, frame.widgetStyle.text, frame.widgetStyle.disabledText))
            return false;
        if(item.enabled){
            const HitTarget target = HitTargetFactory().makeListRowHitTarget(*rectangle, placement.contentClip, enabled, frame.token, key);
            if(!m_builder.m_context.addPartTarget(item.state, *part, target))
                return false;
        }
    }
    return m_builder.listMatches(frame);
}

b8 Builder::paintList(const Item& item, const LayoutBox& box){
    return ListCoordinator(*this).paintList(item, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

