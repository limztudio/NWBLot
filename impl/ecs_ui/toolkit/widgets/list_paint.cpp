// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Builder::ListRow> Builder::prepareListRow(const ListFrame& frame, const u64 index){
    ListRow row(m_arena);
    if(!listMatches(frame) || index >= frame.rowCount)
        return MakeUnexpected(Failure{});
    const u64 key = frame.source->key(index);
    if(key == 0u || !listStateMatches(frame))
        return MakeUnexpected(Failure{});
    const auto resolved = frame.source->indexOf(key);
    if(!resolved || *resolved != index || !listStateMatches(frame))
        return MakeUnexpected(Failure{});
    const b8 enabled = frame.options.enabled && frame.source->enabled(index);
    if(!listStateMatches(frame))
        return MakeUnexpected(Failure{});
    const ShapeRequest request{ frame.source->text(index), StringView(reinterpret_cast<const char*>(frame.measureKey.language.data())), frame.widgetStyle.fontSize, frame.measureKey.scriptTag, TextDirection::LeftToRight };
    if(!listStateMatches(frame))
        return MakeUnexpected(Failure{});
    // The source owns a temporary view; shaping must copy it before any further source callback.
    auto layout = m_text.layout(request);
    if(!layout || !listMatches(frame))
        return MakeUnexpected(Failure{});
    const auto bounds = m_text.measure(*layout, m_paint.displayMetrics());
    if(!bounds)
        return MakeUnexpected(Failure{});
    row.index = index;
    row.key = key;
    row.enabled = enabled;
    row.textBounds = *bounds;
    row.text = Move(*layout);
    if(!listMatches(frame))
        return MakeUnexpected(Failure{});
    return row;
}

Expected<ScrollPlacement> Builder::measureListPlacement(ListFrame& frame, const LayoutBox& box, const Rect& clip){
    auto& rows = frame.rows;
    ListState& state = *frame.state;
    ScrollState scroll;
    if(!listMatches(frame) || !scroll.setOffset(frame.scrollOffset))
        return MakeUnexpected(Failure{});
    const b8 revealCursor = frame.revealCursor && frame.cursorKey != 0u;
    u64 cursorIndex = 0u;
    if(revealCursor){
        const auto resolved = frame.source->indexOf(frame.cursorKey);
        if(!resolved || *resolved >= frame.rowCount || !listStateMatches(frame))
            return MakeUnexpected(Failure{});
        cursorIndex = *resolved;
        if(frame.source->key(cursorIndex) != frame.cursorKey || !listStateMatches(frame) || !frame.source->enabled(cursorIndex) || !listMatches(frame))
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
            const f32 paddedHeight = (row->textBounds.height + frame.style.row.padding.top) + frame.style.row.padding.bottom;
            height = Max(height, paddedHeight);
            rows.insert(found, Move(*row));
        }
        if(!IsFinite(height) || height <= 0.0f || !listMatches(frame))
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

b8 Builder::paintList(const Item& item, const LayoutBox& box){
    if(item.list >= m_scope->m_lists.size())
        return false;
    ListFrame& frame = m_scope->m_lists[item.list];
    if(!listMatches(frame))
        return false;
    const Rect clip = visibleClip(box.clip);
    const auto measuredPlacement = measureListPlacement(frame, box, clip);
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
    const UiSkinRegion* background = region(frame.style.background, frame.style.backgroundFallback);
    if(!background)
        return false;
    m_paint.pushClip(clip);
    const b8 painted = m_paint.drawRegion(background->name, box.rectangle);
    const b8 popped = m_paint.popClip();
    if(!painted || !popped)
        return false;
    HitTarget host;
    host.rectangle = box.rectangle;
    host.clip = clip;
    host.enabled = item.enabled;
    host.focusable = item.enabled;
    host.contextMenu = item.contextMenu;
    host.control = frame.token;
    host.navigable = item.enabled;
    host.scrollable = item.enabled;
    host.scrollStep = static_cast<f64>(frame.rowHeight) * static_cast<f64>(frame.options.wheelRows);
    host.pageRows = ComputeListHostPageRows(placement.viewport.height, frame.rowHeight, frame.rowCount);
    host.gestureMaximum = placement.maxOffset;
    host.focusOnCommit = frame.focusOnCommit;
    if(!m_context.addTarget(item.state, host) || !paintListRows(item, frame, placement))
        return false;
    // Paint owns its glyph pages now; keep no shaped rows alive through later sibling lists.
    frame.rows.clear();
    if(placement.scrollbarVisible){
        const UiSkinRegion* track = region(frame.style.track, frame.style.trackFallback);
        const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollbar);
        if(!thumbId)
            return false;
        const b8 hover = m_context.input().hover() == *thumbId;
        const UiSkinRegion* thumb = region(hover ? frame.style.thumbHover : frame.style.thumb, frame.style.thumbFallback);
        if(!track || !thumb)
            return false;
        m_paint.pushClip(clip);
        const b8 trackPainted = m_paint.drawRegion(track->name, placement.track);
        const b8 thumbPainted = trackPainted && m_paint.drawRegion(thumb->name, placement.thumb);
        const b8 trackPopped = m_paint.popClip();
        if(!thumbPainted || !trackPopped)
            return false;
        if(item.enabled){
            HitTarget target;
            target.rectangle = placement.track;
            target.clip = clip;
            target.control = frame.token;
            const auto trackId = TryMakeWidgetId(item.state.id, s_WidgetPartTrack);
            if(!trackId || !m_context.addPartTarget(item.state, *trackId, target))
                return false;
            target.rectangle = placement.thumb;
            target.pointerGesture = placement.track.height > placement.thumb.height;
            target.gestureReference = placement.track;
            target.gestureMaximum = placement.maxOffset;
            if(!m_context.addPartTarget(item.state, *thumbId, target))
                return false;
        }
    }
    return true;
}

b8 Builder::paintListRows(const Item& item, const ListFrame& frame, const ScrollPlacement& placement){
    const auto& rows = frame.rows;
    const auto rowsId = TryMakeWidgetId(item.state.id, s_WidgetPartRows);
    if(!rowsId)
        return false;
    const b8 focused = m_context.input().focus() == item.state.id || (frame.commandFocus.valid() && m_context.input().focus() == frame.commandFocus);
    auto found = LowerBound(rows.begin(), rows.end(), placement.firstRow, [](const ListRow& row, const u64 value)noexcept{ return row.index < value; });
    for(u64 index = placement.firstRow; index < placement.endRow; ++index, ++found){
        if(found == rows.end() || found->index != index || !listStateMatches(frame))
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
        const SelectablePaintFlags flags{ enabled, frame.state->selectedKey() == key, m_context.input().hover() == *part, focused && frame.state->cursorKey() == key };
        if(!PaintSelectable(m_paint, m_text, *m_skin, row.text, row.textBounds, frame.style.row.padding, *rectangle, placement.contentClip, frame.style.row, flags, frame.widgetStyle.text, frame.widgetStyle.disabledText))
            return false;
        if(item.enabled){
            HitTarget target;
            target.rectangle = *rectangle;
            target.clip = placement.contentClip;
            target.enabled = enabled;
            target.activatable = enabled;
            target.control = frame.token;
            target.value = key;
            if(!m_context.addPartTarget(item.state, *part, target))
                return false;
        }
    }
    return listMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

