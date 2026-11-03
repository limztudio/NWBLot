// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintTextArea(const Item& item, const LayoutBox& box){
    if(item.textArea >= m_scope->m_textAreas.size())
        return false;
    const TextAreaFrame& frame = m_scope->m_textAreas[item.textArea];
    if(!textAreaMatches(frame))
        return false;
    TextAreaState& state = *frame.state;
    const Rect clip = visibleClip(box.clip);
    const f32 caretWidth = 1.0f / m_paint.displayMetrics().pixelScaleX;
    const Rect& visualBounds = item.editView.visualBounds();
    const auto viewport = CalculateScrollViewportPlacement(box.rectangle, clip, item.padding, { visualBounds.width, visualBounds.height }, caretWidth, state.scroll(), item.scrollbarStyle.thickness, item.scrollbarStyle.minimumThumb);
    if(!viewport)
        return false;
    const f32 lineHeight = item.editView.layout().lines().empty() ? item.editView.layout().fontSize() : item.editView.layout().lines().front().height;
    const Point step{ item.editView.layout().fontSize() * frame.wheelLines, lineHeight * frame.wheelLines };
    if(!IsFinite(step.x) || !IsFinite(step.y) || step.x <= 0.0f || step.y <= 0.0f)
        return false;
    const ControlToken token = state.m_scrollInput.prepare(item.state, m_context.popupToken(), state.instanceGeneration(), state.revision(), *frame.model, item.editOptions.enabled, item.editOptions.readOnly, *viewport, step);
    m_context.input().fenceControl(item.state.id, item.state.declarationGeneration, token);
    if(!applyTextAreaScrollInput(item, frame, token))
        return false;
    const auto arranged = item.editView.arrangeViewport(box.rectangle, viewport->viewport, clip, state.scroll(), caretWidth, state.m_revealCaret);
    if(!arranged)
        return false;
    const EditBoxPlacement& placement = *arranged;
    if(!TryUpdateScrollViewportOffsets({ placement.scroll, placement.scrollY }, state.m_scrollInput.m_placement))
        return false;
    if(!item.editView.paint(m_text, m_paint, *m_skin, placement, item.editStyle, item.editFlags) || !textAreaMatches(frame))
        return false;
    state.m_visual.placement = placement;
    state.m_visual.scroll = placement.scroll;
    state.m_scrollY = placement.scrollY;
    HitTarget target;
    target.rectangle = box.rectangle;
    target.clip = visibleClip(box.clip);
    target.enabled = item.editOptions.enabled;
    target.focusable = item.editOptions.enabled;
    target.textEditable = item.editOptions.enabled;
    target.contextMenu = item.contextMenu;
    target.control = token;
    target.scrollable = item.editOptions.enabled;
    target.scrollStep = step.y;
    target.scrollStepX = step.x;
    target.gestureMaximum = state.scrollbars().vertical.maximum;
    target.gestureMaximumX = state.scrollbars().horizontal.maximum;
    if(!m_context.addTarget(item.state, target) || !paintTextAreaScrollbars(item, state.scrollbars(), clip, token))
        return false;
    const b8 published = !m_editHost || m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    if(!published || !textAreaMatches(frame))
        return false;
    return true;
}

b8 Builder::applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token){
    TextAreaState& state = *frame.state;
    const b8 enabled = item.editOptions.enabled;
    const auto horizontalId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollHorizontalThumb);
    if(!horizontalId)
        return false;
    const auto verticalId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollVerticalThumb);
    if(!verticalId)
        return false;
    auto horizontal = m_context.takePartPointerGesture(item.state, *horizontalId, enabled);
    b8 haveHorizontal = horizontal && horizontal->control == token;
    auto vertical = m_context.takePartPointerGesture(item.state, *verticalId, enabled);
    b8 haveVertical = vertical && vertical->control == token;
    auto action = m_context.takeControlAction(item.state, enabled, token);
    b8 haveAction = action.has_value();
    Point scroll = state.scroll();
    b8 manual = false;
    while(haveHorizontal || haveVertical || haveAction){
        const u64 horizontalSequence = haveHorizontal ? horizontal->updateSequence : Limit<u64>::s_Max;
        const u64 verticalSequence = haveVertical ? vertical->updateSequence : Limit<u64>::s_Max;
        const u64 actionSequence = haveAction ? action->id.sequence : Limit<u64>::s_Max;
        if(haveHorizontal && horizontalSequence <= verticalSequence && horizontalSequence <= actionSequence){
            const auto dragged = TryDragScrollbarOffset(*horizontal, true);
            if(!dragged)
                return false;
            scroll.x = *dragged;
            manual = true;
            horizontal = m_context.takePartPointerGesture(item.state, *horizontalId, enabled);
            haveHorizontal = horizontal && horizontal->control == token;
        }
        else if(haveVertical && verticalSequence <= actionSequence){
            const auto dragged = TryDragScrollbarOffset(*vertical, false);
            if(!dragged)
                return false;
            scroll.y = *dragged;
            manual = true;
            vertical = m_context.takePartPointerGesture(item.state, *verticalId, enabled);
            haveVertical = vertical && vertical->control == token;
        }
        else{
            if(!TryApplyTextAreaScrollControlAction(*action, state.scrollbars(), scroll, manual))
                return false;
            action = m_context.takeControlAction(item.state, enabled, token);
            haveAction = action.has_value();
        }
    }
    if(manual){
        state.m_visual.scroll = scroll.x;
        state.m_scrollY = scroll.y;
        state.m_revealCaret = false;
    }
    return true;
}

b8 Builder::paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement, const Rect& clip, const ControlToken& token){
    const b8 enabled = item.editOptions.enabled;
    m_paint.pushClip(clip);
    b8 painted = true;
    for(u32 axis = 0u; axis < s_ScrollAxisCount && painted; ++axis){
        const b8 horizontal = axis == 0u;
        const ScrollbarPlacement& bar = horizontal ? placement.horizontal : placement.vertical;
        if(!bar.visible)
            continue;
        const auto thumbId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalThumb : s_WidgetPartScrollVerticalThumb);
        if(!thumbId){
            painted = false;
            break;
        }
        const b8 hovered = m_context.input().hover() == *thumbId;
        const b8 pressed = m_context.input().capture() == *thumbId;
        const Name thumbName = ResolveScrollbarThumbRegionName(item.scrollbarStyle, enabled, pressed, hovered);
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
        const f32 trackExtent = horizontal ? bar.track.width : bar.track.height;
        const f32 thumbExtent = horizontal ? bar.thumb.width : bar.thumb.height;
        Rect beforeRect;
        Rect afterRect;
        ResolveScrollbarBeforeAfterRects(bar, horizontal, beforeRect, afterRect);
        HitTarget target;
        target.clip = clip;
        target.control = token;
        target.activatable = true;
        target.value = horizontal ? s_TextAreaScrollHorizontalBefore : s_TextAreaScrollVerticalBefore;
        target.rectangle = beforeRect;
        const auto beforeId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalBefore : s_WidgetPartScrollVerticalBefore);
        if(!beforeId || !m_context.addPartTarget(item.state, *beforeId, target))
            return false;
        target.value = horizontal ? s_TextAreaScrollHorizontalAfter : s_TextAreaScrollVerticalAfter;
        target.rectangle = afterRect;
        const auto afterId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalAfter : s_WidgetPartScrollVerticalAfter);
        if(!afterId || !m_context.addPartTarget(item.state, *afterId, target))
            return false;
        target.rectangle = bar.thumb;
        target.activatable = false;
        target.value = 0u;
        target.pointerGesture = trackExtent > thumbExtent;
        target.gestureReference = bar.track;
        target.gestureMaximum = bar.maximum;
        const auto thumbId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalThumb : s_WidgetPartScrollVerticalThumb);
        if(!thumbId || !m_context.addPartTarget(item.state, *thumbId, target))
            return false;
    }
    if(placement.corner.width > 0.0f && placement.corner.height > 0.0f){
        HitTarget target;
        target.rectangle = placement.corner;
        target.clip = clip;
        target.control = token;
        const auto cornerId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollCorner);
        if(!cornerId || !m_context.addPartTarget(item.state, *cornerId, target))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

