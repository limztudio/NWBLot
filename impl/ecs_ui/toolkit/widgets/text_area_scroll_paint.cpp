// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement, const Rect& clip, const ControlToken& token){
    const b8 enabled = item.editOptions.enabled;
    m_paint.pushClip(clip);
    b8 painted = true;
    for(u32 axis = 0u; axis < s_ScrollAxisCount && painted; ++axis){
        const b8 horizontal = axis == 0u;
        const ScrollbarPlacement& bar = horizontal ? placement.horizontal : placement.vertical;
        if(!bar.visible)
            continue;
        WidgetId thumbId;
        if(!TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalThumb : s_WidgetPartScrollVerticalThumb, thumbId)){
            painted = false;
            break;
        }
        const b8 hovered = m_context.input().hover() == thumbId;
        const b8 pressed = m_context.input().capture() == thumbId;
        const Name& thumbName = !enabled ? item.scrollbarStyle.thumbDisabled : pressed ? item.scrollbarStyle.thumbPressed : hovered ? item.scrollbarStyle.thumbHover : item.scrollbarStyle.thumb;
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
    const b8 popped = m_paint.popClip();
    if(!painted || !popped)
        return false;
    if(!enabled)
        return true;
    for(u32 axis = 0u; axis < s_ScrollAxisCount; ++axis){
        const b8 horizontal = axis == 0u;
        const ScrollbarPlacement& bar = horizontal ? placement.horizontal : placement.vertical;
        if(!bar.visible)
            continue;
        const f32 start = horizontal ? bar.track.x : bar.track.y;
        const f32 thumbStart = horizontal ? bar.thumb.x : bar.thumb.y;
        const f32 trackExtent = horizontal ? bar.track.width : bar.track.height;
        const f32 thumbExtent = horizontal ? bar.thumb.width : bar.thumb.height;
        const f32 before = Max(0.0f, thumbStart - start);
        const f32 after = Max(0.0f, ((start + trackExtent) - thumbStart) - thumbExtent);
        HitTarget target;
        target.clip = clip;
        target.control = token;
        target.activatable = true;
        target.value = horizontal ? s_TextAreaScrollHorizontalBefore : s_TextAreaScrollVerticalBefore;
        target.rectangle = bar.track;
        if(horizontal)
            target.rectangle.width = before;
        else
            target.rectangle.height = before;
        WidgetId beforeId;
        if(!TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalBefore : s_WidgetPartScrollVerticalBefore, beforeId) || !m_context.addPartTarget(item.state, beforeId, target))
            return false;
        target.value = horizontal ? s_TextAreaScrollHorizontalAfter : s_TextAreaScrollVerticalAfter;
        target.rectangle = bar.track;
        if(horizontal){
            target.rectangle.x = thumbStart + thumbExtent;
            target.rectangle.width = after;
        }
        else{
            target.rectangle.y = thumbStart + thumbExtent;
            target.rectangle.height = after;
        }
        WidgetId afterId;
        if(!TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalAfter : s_WidgetPartScrollVerticalAfter, afterId) || !m_context.addPartTarget(item.state, afterId, target))
            return false;
        target.rectangle = bar.thumb;
        target.activatable = false;
        target.value = 0u;
        target.pointerGesture = trackExtent > thumbExtent;
        target.gestureReference = bar.track;
        target.gestureMaximum = bar.maximum;
        WidgetId thumbId;
        if(!TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalThumb : s_WidgetPartScrollVerticalThumb, thumbId) || !m_context.addPartTarget(item.state, thumbId, target))
            return false;
    }
    if(placement.corner.width > 0.0f && placement.corner.height > 0.0f){
        HitTarget target;
        target.rectangle = placement.corner;
        target.clip = clip;
        target.control = token;
        WidgetId cornerId;
        if(!TryMakeWidgetId(item.state.id, s_WidgetPartScrollCorner, cornerId) || !m_context.addPartTarget(item.state, cornerId, target))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

