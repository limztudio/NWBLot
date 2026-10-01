// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "selectable_paint.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintList(const Item& item, const LayoutBox& box){
    if(item.list >= m_scope->m_lists.size())
        return false;
    ListFrame& frame = m_scope->m_lists[item.list];
    if(!listMatches(frame))
        return false;
    const Rect clip = visibleClip(box.clip);
    ScrollPlacement placement;
    if(!ScrollLayout::Calculate(
        box.rectangle, clip, frame.padding, frame.style.scrollbarWidth, frame.style.minimumThumb,
        frame.rowCount, frame.options.rowHeight, frame.state->scrollOffset(), placement
    ))
        return false;
    if(
        !ListBehavior::EnsureCursor(*frame.state, *frame.source, frame.options.rowHeight, placement.viewport.height)
        || !listStateMatches(frame)
    )
        return false;
    if(!ScrollLayout::Calculate(
        box.rectangle, clip, frame.padding, frame.style.scrollbarWidth, frame.style.minimumThumb,
        frame.rowCount, frame.options.rowHeight, frame.state->scrollOffset(), placement
    ))
        return false;
    // The dataset never determines allocation or target capacity; excessive visible density rejects this frame.
    if(placement.endRow - placement.firstRow > 1024u || !frame.state->m_scroll.setOffset(placement.offset))
        return false;
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
    host.scrollStep = static_cast<f64>(frame.options.rowHeight) * static_cast<f64>(frame.options.wheelRows);
    const f64 page = Floor(static_cast<f64>(placement.viewport.height) / static_cast<f64>(frame.options.rowHeight));
    host.pageRows = page >= static_cast<f64>(frame.rowCount) ? Max<u64>(1u, frame.rowCount)
        : Max<u64>(1u, static_cast<u64>(page));
    host.gestureMaximum = placement.maxOffset;
    host.focusOnCommit = frame.focusOnCommit;
    if(!m_context.addTarget(item.state, host) || !paintListRows(item, frame, placement))
        return false;
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
    const WidgetId rows = MakeWidgetId(item.state.id, "rows");
    const bool focused = m_context.input().focus() == item.state.id
        || (frame.keyboardFocus.valid() && m_context.input().focus() == frame.keyboardFocus);
    TextLayout text(m_arena);
    for(u64 index = placement.firstRow; index < placement.endRow; ++index){
        if(!listStateMatches(frame))
            return false;
        const u64 key = frame.source->key(index);
        if(!listStateMatches(frame))
            return false;
        u64 resolved = 0u;
        if(key == 0u || !frame.source->indexOf(key, resolved) || !listStateMatches(frame) || resolved != index)
            return false;
        const bool enabled = item.enabled && frame.source->enabled(index);
        if(!listStateMatches(frame))
            return false;
        const WidgetId part = MakeWidgetPartId(rows, key);
        Rect rectangle;
        if(!ScrollLayout::RowBounds(index, placement, frame.options.rowHeight, rectangle))
            return false;
        const ShapeRequest request = textShapeRequest(frame.source->text(index), frame.widgetStyle.fontSize);
        if(!listStateMatches(frame) || m_text.layout(request, text) != TextLayoutStatus::Success)
            return false;
        const SelectablePaintFlags flags{ enabled, frame.state->selectedKey() == key,
            m_context.input().hover() == part, focused && frame.state->cursorKey() == key };
        if(!SelectablePainter::Paint(
            m_paint, m_text, *m_skin, text, rectangle, placement.contentClip, frame.style.row, flags,
            frame.widgetStyle.text, frame.widgetStyle.disabledText
        ))
            return false;
        if(item.enabled){
            HitTarget target;
            target.rectangle = rectangle;
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

