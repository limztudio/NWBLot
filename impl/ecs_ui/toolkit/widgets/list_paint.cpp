// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "selectable_paint.h"

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::prepareListRow(const ListFrame& frame, const u64 index, ListRow& row){
    if(!listMatches(frame) || index >= frame.rowCount)
        return false;
    const u64 key = frame.source->key(index);
    if(key == 0u || !listStateMatches(frame))
        return false;
    const auto resolved = frame.source->indexOf(key);
    if(!resolved || *resolved != index || !listStateMatches(frame))
        return false;
    const bool enabled = frame.options.enabled && frame.source->enabled(index);
    if(!listStateMatches(frame))
        return false;
    const ShapeRequest request{ frame.source->text(index), frame.widgetStyle.fontSize, TextDirection::LeftToRight, frame.measureKey.scriptTag, StringView(frame.measureKey.language.data()) };
    if(!listStateMatches(frame))
        return false;
    // The source owns a temporary view; shaping must copy it before any further source callback.
    auto layout = m_text.layout(request);
    if(!layout || !listMatches(frame))
        return false;
    const auto bounds = m_text.measure(*layout, m_paint.displayMetrics());
    if(!bounds)
        return false;
    row.index = index;
    row.key = key;
    row.enabled = enabled;
    row.textBounds = *bounds;
    row.text = Move(*layout);
    return listMatches(frame);
}

bool Builder::measureListPlacement(ListFrame& frame, const LayoutBox& box, const Rect& clip, ScrollPlacement& placement){
    auto& rows = frame.rows;
    ListState& state = *frame.state;
    ScrollState scroll;
    if(!listMatches(frame) || !scroll.setOffset(frame.scrollOffset))
        return false;
    const bool revealCursor = frame.revealCursor && frame.cursorKey != 0u;
    u64 cursorIndex = 0u;
    if(revealCursor){
        const auto resolved = frame.source->indexOf(frame.cursorKey);
        if(!resolved || *resolved >= frame.rowCount || !listStateMatches(frame))
            return false;
        cursorIndex = *resolved;
        if(frame.source->key(cursorIndex) != frame.cursorKey || !listStateMatches(frame) || !frame.source->enabled(cursorIndex) || !listMatches(frame))
            return false;
    }
    for(;;){
        auto candidate = ScrollLayout::Calculate(box.rectangle, clip, frame.padding, frame.style.scrollbarWidth, frame.style.minimumThumb, frame.rowCount, frame.rowHeight, scroll.offset());
        if(!candidate)
            return false;
        if(revealCursor){
            const SIMDVectorDouble startValue = VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(cursorIndex)), VectorDoubleReplicate(frame.rowHeight));
            const f64 start = VectorDoubleGetX(startValue);
            const f64 end = VectorDoubleGetX(VectorDoubleAdd(startValue, VectorDoubleReplicate(frame.rowHeight)));
            if(!IsFinite(start) || !IsFinite(end) || end <= start || !scroll.ensureVisible(start, end, candidate->viewport.height))
                return false;
        }
        if(revealCursor)
            candidate = ScrollLayout::Calculate(box.rectangle, clip, frame.padding, frame.style.scrollbarWidth, frame.style.minimumThumb, frame.rowCount, frame.rowHeight, scroll.offset());
        if(!candidate || candidate->endRow - candidate->firstRow > 1024u)
            return false;
        f32 height = frame.rowHeight;
        for(u64 index = candidate->firstRow; index < candidate->endRow; ++index){
            const auto found = LowerBound(rows.begin(), rows.end(), index, [](const ListRow& row, const u64 value)noexcept{ return row.index < value; });
            if(found != rows.end() && found->index == index)
                continue;
            // The work bound is independent of dataset size, even when a cursor reveal introduces a new interval.
            if(rows.size() >= 1024u)
                return false;
            ListRow row(m_arena);
            if(!prepareListRow(frame, index, row))
                return false;
            const SIMDVectorSingle paddedSize = VectorAdd(VectorAdd(VectorSet(row.textBounds.width, row.textBounds.height, 0.0f, 0.0f), VectorSet(frame.style.row.padding.left, frame.style.row.padding.top, 0.0f, 0.0f)), VectorSet(frame.style.row.padding.right, frame.style.row.padding.bottom, 0.0f, 0.0f));
            height = VectorGetY(VectorMax(VectorSet(0.0f, height, 0.0f, 0.0f), paddedSize));
            rows.insert(found, Move(row));
        }
        if(!IsFinite(height) || height <= 0.0f || !listMatches(frame))
            return false;
        if(height == frame.rowHeight){
            frame.scrollOffset = candidate->offset;
            // Another declaration can share this state with a different sizing context.
            if(state.m_measureKey == frame.measureKey)
                state.m_rowHeight = VectorGetX(VectorMax(VectorReplicate(state.m_rowHeight), VectorReplicate(frame.rowHeight)));
            placement = *candidate;
            return true;
        }
        // Retain the first visible row and its pixel offset at the inherited clip edge while the uniform estimate grows.
        const SIMDVectorDouble clippedTop = VectorDoubleSubtract(VectorDoubleReplicate(candidate->contentClip.y), VectorDoubleReplicate(candidate->viewport.y));
        const SIMDVectorDouble rowOffsets = VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(candidate->firstRow)), VectorDoubleSet(frame.rowHeight, height, 0.0, 0.0));
        const SIMDVectorDouble within = VectorDoubleSubtract(VectorDoubleAdd(VectorDoubleReplicate(candidate->offset), clippedTop), VectorDoubleSplatX(rowOffsets));
        const SIMDVectorDouble offset = VectorDoubleSubtract(VectorDoubleAdd(VectorDoubleSplatY(rowOffsets), within), clippedTop);
        if(!scroll.setOffset(VectorDoubleGetX(VectorDoubleMax(VectorDoubleZero(), offset))))
            return false;
        frame.rowHeight = height;
    }
}

bool Builder::paintList(const Item& item, const LayoutBox& box){
    if(item.list >= m_scope->m_lists.size())
        return false;
    ListFrame& frame = m_scope->m_lists[item.list];
    if(!listMatches(frame))
        return false;
    const Rect clip = visibleClip(box.clip);
    ScrollPlacement placement;
    if(!measureListPlacement(frame, box, clip, placement))
        return false;
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
    const bool painted = m_paint.drawRegion(background->name, box.rectangle);
    const bool popped = m_paint.popClip();
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
    host.scrollStep = VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(frame.rowHeight), VectorDoubleReplicate(frame.options.wheelRows)));
    const f64 page = VectorDoubleGetX(VectorDoubleFloor(VectorDoubleDivide(VectorDoubleReplicate(placement.viewport.height), VectorDoubleReplicate(frame.rowHeight))));
    host.pageRows = page >= static_cast<f64>(frame.rowCount) ? Max<u64>(1u, frame.rowCount) : Max<u64>(1u, static_cast<u64>(page));
    host.gestureMaximum = placement.maxOffset;
    host.focusOnCommit = frame.focusOnCommit;
    if(!m_context.addTarget(item.state, host) || !paintListRows(item, frame, placement))
        return false;
    // Paint owns its glyph pages now; keep no shaped rows alive through later sibling lists.
    frame.rows.clear();
    if(placement.scrollbarVisible){
        const UiSkinRegion* track = region(frame.style.track, frame.style.trackFallback);
        const WidgetId thumbId = MakeWidgetId(item.state.id, "scrollbar");
        const bool hover = m_context.input().hover() == thumbId;
        const UiSkinRegion* thumb = region(hover ? frame.style.thumbHover : frame.style.thumb, frame.style.thumbFallback);
        if(!track || !thumb)
            return false;
        m_paint.pushClip(clip);
        const bool trackPainted = m_paint.drawRegion(track->name, placement.track);
        const bool thumbPainted = trackPainted && m_paint.drawRegion(thumb->name, placement.thumb);
        const bool trackPopped = m_paint.popClip();
        if(!thumbPainted || !trackPopped)
            return false;
        if(item.enabled){
            HitTarget target;
            target.rectangle = placement.track;
            target.clip = clip;
            target.control = frame.token;
            if(!m_context.addPartTarget(item.state, MakeWidgetId(item.state.id, "track"), target))
                return false;
            target.rectangle = placement.thumb;
            target.pointerGesture = placement.track.height > placement.thumb.height;
            target.gestureReference = placement.track;
            target.gestureMaximum = placement.maxOffset;
            if(!m_context.addPartTarget(item.state, thumbId, target))
                return false;
        }
    }
    return true;
}

bool Builder::paintListRows(const Item& item, const ListFrame& frame, const ScrollPlacement& placement){
    const auto& rows = frame.rows;
    const WidgetId rowsId = MakeWidgetId(item.state.id, "rows");
    const bool focused = m_context.input().focus() == item.state.id || (frame.commandFocus.valid() && m_context.input().focus() == frame.commandFocus);
    auto found = LowerBound(rows.begin(), rows.end(), placement.firstRow, [](const ListRow& row, const u64 value)noexcept{ return row.index < value; });
    for(u64 index = placement.firstRow; index < placement.endRow; ++index, ++found){
        if(found == rows.end() || found->index != index || !listStateMatches(frame))
            return false;
        const ListRow& row = *found;
        const u64 key = row.key;
        const bool enabled = item.enabled && row.enabled;
        const WidgetId part = MakeWidgetPartId(rowsId, key);
        const auto rectangle = ScrollLayout::RowBounds(index, placement, frame.rowHeight);
        if(!rectangle)
            return false;
        const SelectablePaintFlags flags{ enabled, frame.state->selectedKey() == key, m_context.input().hover() == part, focused && frame.state->cursorKey() == key };
        if(!SelectablePainter::Paint(m_paint, m_text, *m_skin, row.text, row.textBounds, frame.style.row.padding, *rectangle, placement.contentClip, frame.style.row, flags, frame.widgetStyle.text, frame.widgetStyle.disabledText))
            return false;
        if(item.enabled){
            HitTarget target;
            target.rectangle = *rectangle;
            target.clip = placement.contentClip;
            target.enabled = enabled;
            target.activatable = enabled;
            target.control = frame.token;
            target.value = key;
            if(!m_context.addPartTarget(item.state, part, target))
                return false;
        }
    }
    return listMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

