// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/list_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/list_layout_preparer.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

