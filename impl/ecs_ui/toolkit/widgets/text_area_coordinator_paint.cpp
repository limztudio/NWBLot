// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/text_area_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TextAreaCoordinator::paintTextArea(const Item& item, const LayoutBox& box){
    if(item.textArea >= m_builder.m_scope->m_textAreas.size())
        return false;
    const TextAreaFrame& frame = m_builder.m_scope->m_textAreas[item.textArea];
    if(!m_builder.textAreaMatches(frame))
        return false;
    TextAreaState& state = *frame.state;
    const Rect clip = m_builder.visibleClip(box.clip);
    const f32 caretWidth = 1.0f / m_builder.m_paint.displayMetrics().pixelScaleX;
    const Rect& visualBounds = item.editView.visualBounds();
    const auto viewport = CalculateScrollViewportPlacement(box.rectangle, clip, item.padding, { visualBounds.width, visualBounds.height }, caretWidth, state.scroll(), item.scrollbarStyle.thickness, item.scrollbarStyle.minimumThumb);
    if(!viewport)
        return false;
    const f32 lineHeight = item.editView.layout().lines().empty() ? item.editView.layout().fontSize() : item.editView.layout().lines().front().height;
    const auto stepResult = TryResolveTextAreaScrollStep(item.editView.layout().fontSize(), lineHeight, frame.wheelLines);
    if(!stepResult)
        return false;
    const Point step = *stepResult;
    const ControlToken token = state.m_scrollInput.prepare(item.state, m_builder.m_context.popupToken(), state.instanceGeneration(), state.revision(), *frame.model, item.editOptions.enabled, item.editOptions.readOnly, *viewport, step);
    m_builder.m_context.input().fenceControl(item.state.id, item.state.declarationGeneration, token);
    if(!applyTextAreaScrollInput(item, frame, token))
        return false;
    const auto arranged = item.editView.arrangeViewport(box.rectangle, viewport->viewport, clip, state.scroll(), caretWidth, state.m_revealCaret);
    if(!arranged)
        return false;
    const EditBoxPlacement& placement = *arranged;
    if(!TryUpdateScrollViewportOffsets({ placement.scroll, placement.scrollY }, state.m_scrollInput.m_placement))
        return false;
    if(!item.editView.paint(m_builder.m_text, m_builder.m_paint, *m_builder.m_skin, placement, item.editStyle, item.editFlags) || !m_builder.textAreaMatches(frame))
        return false;
    state.m_visual.placement = placement;
    state.m_visual.scroll = placement.scroll;
    state.m_scrollY = placement.scrollY;
    const HitTarget target = HitTargetFactory().makeTextAreaHitTarget(
        box.rectangle,
        m_builder.visibleClip(box.clip),
        item.editOptions.enabled,
        item.contextMenu,
        token,
        step,
        state.scrollbars()
    );
    if(!m_builder.m_context.addTarget(item.state, target) || !paintTextAreaScrollbars(item, state.scrollbars(), clip, token))
        return false;
    const b8 published = !m_builder.m_editHost || m_builder.m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    if(!published || !m_builder.textAreaMatches(frame))
        return false;
    return true;
}

b8 TextAreaCoordinator::applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token){
    TextAreaState& state = *frame.state;
    const b8 enabled = item.editOptions.enabled;
    const auto horizontalId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollHorizontalThumb);
    if(!horizontalId)
        return false;
    const auto verticalId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollVerticalThumb);
    if(!verticalId)
        return false;
    auto horizontal = m_builder.m_context.takePartPointerGesture(item.state, *horizontalId, enabled);
    b8 haveHorizontal = horizontal && horizontal->control == token;
    auto vertical = m_builder.m_context.takePartPointerGesture(item.state, *verticalId, enabled);
    b8 haveVertical = vertical && vertical->control == token;
    auto action = m_builder.m_context.takeControlAction(item.state, enabled, token);
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
            horizontal = m_builder.m_context.takePartPointerGesture(item.state, *horizontalId, enabled);
            haveHorizontal = horizontal && horizontal->control == token;
        }
        else if(haveVertical && verticalSequence <= actionSequence){
            const auto dragged = TryDragScrollbarOffset(*vertical, false);
            if(!dragged)
                return false;
            scroll.y = *dragged;
            manual = true;
            vertical = m_builder.m_context.takePartPointerGesture(item.state, *verticalId, enabled);
            haveVertical = vertical && vertical->control == token;
        }
        else{
            if(!TryApplyTextAreaScrollControlAction(*action, state.scrollbars(), scroll, manual))
                return false;
            action = m_builder.m_context.takeControlAction(item.state, enabled, token);
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

b8 TextAreaCoordinator::paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement, const Rect& clip, const ControlToken& token){
    const b8 enabled = item.editOptions.enabled;
    b8 horizontalHovered = false;
    b8 horizontalPressed = false;
    if(placement.horizontal.visible){
        const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollHorizontalThumb);
        if(!thumbId)
            return false;
        horizontalHovered = m_builder.m_context.input().hover() == *thumbId;
        horizontalPressed = m_builder.m_context.input().capture() == *thumbId;
    }
    b8 verticalHovered = false;
    b8 verticalPressed = false;
    if(placement.vertical.visible){
        const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollVerticalThumb);
        if(!thumbId)
            return false;
        verticalHovered = m_builder.m_context.input().hover() == *thumbId;
        verticalPressed = m_builder.m_context.input().capture() == *thumbId;
    }
    if(!ControlVisualPainter().paintTextAreaScrollbarsVisual(
        m_builder.m_paint,
        *m_builder.m_skin,
        item.scrollbarStyle,
        placement,
        clip,
        enabled,
        horizontalHovered,
        horizontalPressed,
        verticalHovered,
        verticalPressed
    ))
        return false;
    if(!enabled)
        return true;
    for(u32 axis = 0u; axis < s_ScrollAxisCount; ++axis){
        const b8 horizontal = axis == 0u;
        const ScrollbarPlacement& bar = horizontal ? placement.horizontal : placement.vertical;
        if(!bar.visible)
            continue;
        Rect beforeRect;
        Rect afterRect;
        ResolveScrollbarBeforeAfterRects(bar, horizontal, beforeRect, afterRect);
        const HitTarget beforeTarget = HitTargetFactory().makeScrollbarTrackActionHitTarget(
            beforeRect,
            clip,
            token,
            horizontal ? s_TextAreaScrollHorizontalBefore : s_TextAreaScrollVerticalBefore
        );
        const auto beforeId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalBefore : s_WidgetPartScrollVerticalBefore);
        if(!beforeId || !m_builder.m_context.addPartTarget(item.state, *beforeId, beforeTarget))
            return false;
        const HitTarget afterTarget = HitTargetFactory().makeScrollbarTrackActionHitTarget(
            afterRect,
            clip,
            token,
            horizontal ? s_TextAreaScrollHorizontalAfter : s_TextAreaScrollVerticalAfter
        );
        const auto afterId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalAfter : s_WidgetPartScrollVerticalAfter);
        if(!afterId || !m_builder.m_context.addPartTarget(item.state, *afterId, afterTarget))
            return false;
        const HitTarget thumbTarget = HitTargetFactory().makeScrollbarThumbHitTarget(bar, clip, token, horizontal);
        const auto thumbId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalThumb : s_WidgetPartScrollVerticalThumb);
        if(!thumbId || !m_builder.m_context.addPartTarget(item.state, *thumbId, thumbTarget))
            return false;
    }
    if(placement.corner.width > 0.0f && placement.corner.height > 0.0f){
        const HitTarget target = HitTargetFactory().makeScrollbarCornerHitTarget(placement.corner, clip, token);
        const auto cornerId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollCorner);
        if(!cornerId || !m_builder.m_context.addPartTarget(item.state, *cornerId, target))
            return false;
    }
    return true;
}

b8 Builder::paintTextArea(const Item& item, const LayoutBox& box){
    return TextAreaCoordinator(*this).paintTextArea(item, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

