// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/list_layout_preparer.h>
#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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
            const f32 paddedHeight = (row->textBounds.height + frame.style.row.padding.top) + frame.style.row.padding.bottom;
            height = Max(height, paddedHeight);
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


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

