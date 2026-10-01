// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement,
    const Rect& clip, const ControlToken& token){
    const bool enabled = item.editOptions.enabled;
    m_paint.pushClip(clip);
    bool painted = true;
    for(u32 axis = 0u; axis < 2u && painted; ++axis){
        const bool horizontal = axis == 0u;
        const ScrollbarPlacement& bar = horizontal ? placement.horizontal : placement.vertical;
        if(!bar.visible)
            continue;
        const WidgetId thumbId = MakeWidgetId(item.state.id, horizontal ? "scroll.x.thumb" : "scroll.y.thumb");
        const bool hovered = m_context.input().hover() == thumbId;
        const bool pressed = m_context.input().capture() == thumbId;
        const Name& thumbName = !enabled ? item.scrollbarStyle.thumbDisabled : pressed ? item.scrollbarStyle.thumbPressed
            : hovered ? item.scrollbarStyle.thumbHover : item.scrollbarStyle.thumb;
        const UiSkinRegion* track = region(item.scrollbarStyle.track, item.scrollbarStyle.trackFallback);
        const UiSkinRegion* thumb = region(thumbName, item.scrollbarStyle.thumbFallback);
        if(track)
            painted = m_paint.drawRegion(track->name, bar.track);
        else
            m_paint.fillRect(bar.track, item.scrollbarStyle.trackColor);
        if(painted && thumb)
            painted = m_paint.drawRegion(thumb->name, bar.thumb);
        else if(painted)
            m_paint.fillRect(bar.thumb, enabled ? item.scrollbarStyle.thumbColor : item.scrollbarStyle.disabledColor);
    }
    if(painted && placement.corner.width > 0.0f && placement.corner.height > 0.0f){
        if(const UiSkinRegion* track = region(item.scrollbarStyle.track, item.scrollbarStyle.trackFallback))
            painted = m_paint.drawRegion(track->name, placement.corner);
        else
            m_paint.fillRect(placement.corner, item.scrollbarStyle.trackColor);
    }
    const bool popped = m_paint.popClip();
    if(!painted || !popped)
        return false;
    if(!enabled)
        return true;
    for(u32 axis = 0u; axis < 2u; ++axis){
        const bool horizontal = axis == 0u;
        const ScrollbarPlacement& bar = horizontal ? placement.horizontal : placement.vertical;
        if(!bar.visible)
            continue;
        const f32 start = horizontal ? bar.track.x : bar.track.y;
        const f32 thumbStart = horizontal ? bar.thumb.x : bar.thumb.y;
        const f32 trackExtent = horizontal ? bar.track.width : bar.track.height;
        const f32 thumbExtent = horizontal ? bar.thumb.width : bar.thumb.height;
        const f32 before = Max(0.0f, thumbStart - start);
        const f32 after = Max(0.0f, start + trackExtent - thumbStart - thumbExtent);
        HitTarget target;
        target.clip = clip;
        target.control = token;
        target.activatable = true;
        target.value = horizontal ? 1u : 3u;
        target.rectangle = bar.track;
        if(horizontal)
            target.rectangle.width = before;
        else
            target.rectangle.height = before;
        if(!m_context.addPartTarget(item.state, MakeWidgetId(item.state.id, horizontal ? "scroll.x.before" : "scroll.y.before"), target))
            return false;
        target.value = horizontal ? 2u : 4u;
        target.rectangle = bar.track;
        if(horizontal){
            target.rectangle.x = thumbStart + thumbExtent;
            target.rectangle.width = after;
        }
        else{
            target.rectangle.y = thumbStart + thumbExtent;
            target.rectangle.height = after;
        }
        if(!m_context.addPartTarget(item.state, MakeWidgetId(item.state.id, horizontal ? "scroll.x.after" : "scroll.y.after"), target))
            return false;
        target.rectangle = bar.thumb;
        target.activatable = false;
        target.value = 0u;
        target.pointerGesture = trackExtent > thumbExtent;
        target.gestureReference = bar.track;
        target.gestureMaximum = bar.maximum;
        if(!m_context.addPartTarget(item.state, MakeWidgetId(item.state.id, horizontal ? "scroll.x.thumb" : "scroll.y.thumb"), target))
            return false;
    }
    if(placement.corner.width > 0.0f && placement.corner.height > 0.0f){
        HitTarget target;
        target.rectangle = placement.corner;
        target.clip = clip;
        target.control = token;
        if(!m_context.addPartTarget(item.state, MakeWidgetId(item.state.id, "scroll.corner"), target))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

