// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintSlider(const Item& item, const LayoutBox& box){
    if(item.slider >= m_scope->m_sliders.size())
        return false;
    SliderFrame& frame = m_scope->m_sliders[item.slider];
    if(!sliderMatches(frame))
        return false;
    auto normalized = SliderBehavior::Normalize(frame.m_options.minimum, frame.m_options.maximum, frame.m_state.value());
    if(!normalized)
        return false;
    const Rect clip = visibleClip(box.clip);
    auto placement = PlaceSlider(box.rectangle, clip, frame.m_metrics, *normalized);
    if(!placement || !SliderBehavior::Admit(frame.m_state, frame.m_options, *placement))
        return false;
    frame.m_snapshot = frame.m_state.snapshot();
    frame.m_token = frame.m_state.controlToken();
    const b8 interactive = frame.m_options.enabled && frame.m_options.minimum != frame.m_options.maximum
        && placement->centerTravel.width > 0.0f && placement->thumb.width > 0.0f && placement->thumb.height > 0.0f
        && placement->clip.width > 0.0f && placement->clip.height > 0.0f;
    InputRouter& input = m_context.input();
    const b8 previouslyFocused = input.focus() == item.state.id;
    // Prospective geometry and policy fence the displayed lifetime before any copied intention changes the value.
    input.fenceControl(item.state.id, item.state.declarationGeneration, frame.m_token);
    if(!interactive)
        input.invalidateTarget(item.state.id);
    frame.m_focusOnCommit = interactive && previouslyFocused && input.focus() != item.state.id;
    frame.m_interactive = interactive;
    if(!applySliderInput(frame, *placement, interactive))
        return false;
    normalized = SliderBehavior::Normalize(frame.m_options.minimum, frame.m_options.maximum, frame.m_state.value());
    if(!normalized)
        return false;
    placement = PlaceSlider(box.rectangle, clip, frame.m_metrics, *normalized);
    if(!placement)
        return false;
    const auto trackId = TryMakeWidgetId(item.state.id, s_WidgetPartTrack);
    if(!trackId)
        return false;
    const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartThumb);
    if(!thumbId)
        return false;
    const b8 hover = interactive && input.hover() == *thumbId;
    const b8 pressed = interactive && input.primaryDown() && (input.capture() == *trackId || input.capture() == *thumbId);
    const UiSkinRegion* track = region(frame.m_style.track, frame.m_style.trackFallback);
    const UiSkinRegion* thumb = !interactive ? m_skin->findRegion(frame.m_style.disabled) : pressed ? m_skin->findRegion(frame.m_style.pressed) : hover ? m_skin->findRegion(frame.m_style.hover) : nullptr;
    if(!thumb)
        thumb = region(frame.m_style.normal, frame.m_style.thumbFallback);
    if(!track || !thumb)
        return false;
    const Color tint = !interactive ? frame.m_style.disabledTint : pressed ? frame.m_style.pressedTint : hover ? frame.m_style.hoverTint : Color{};
    m_paint.pushClip(placement->clip);
    b8 painted = m_paint.drawRegion(track->name, placement->track, interactive ? Color{} : frame.m_style.disabledTint);
    if(painted)
        painted = m_paint.drawRegion(thumb->name, placement->thumb, tint);
    if(painted && interactive && input.focus() == item.state.id && m_skin->findRegion(frame.m_style.focus))
        painted = m_paint.drawRegion(frame.m_style.focus, placement->bounds);
    const b8 popped = m_paint.popClip();
    if(!painted || !popped || !sliderMatches(frame))
        return false;
    HitTarget host;
    host.rectangle = placement->bounds;
    host.clip = placement->clip;
    host.enabled = interactive;
    host.focusable = interactive;
    host.navigable = interactive;
    host.horizontalNavigation = interactive;
    host.contextMenu = item.contextMenu;
    host.control = frame.m_token;
    host.focusOnCommit = frame.m_focusOnCommit;
    if(!m_context.addTarget(item.state, host))
        return false;
    if(interactive){
        HitTarget trackTarget;
        trackTarget.rectangle = placement->track;
        trackTarget.clip = placement->clip;
        trackTarget.pointerGesture = true;
        trackTarget.gestureReference = placement->centerTravel;
        trackTarget.control = frame.m_token;
        if(!m_context.addPartTarget(item.state, *trackId, trackTarget))
            return false;
        HitTarget thumbTarget;
        thumbTarget.rectangle = placement->thumb;
        thumbTarget.clip = placement->clip;
        thumbTarget.pointerGesture = true;
        thumbTarget.gestureReference = placement->travelBounds;
        thumbTarget.control = frame.m_token;
        thumbTarget.value = BitCast<u64>(frame.m_state.value());
        if(!m_context.addPartTarget(item.state, *thumbId, thumbTarget))
            return false;
    }
    if(!sliderMatches(frame))
        return false;
    frame.m_state.m_placement = *placement;
    frame.m_result.valid = true;
    return true;
}

b8 Builder::applySliderInput(SliderFrame& frame, const SliderPlacement& placement, const b8 interactive){
    if(!sliderMatches(frame))
        return false;
    if(!interactive || placement.centerTravel.width <= 0.0f)
        return true;
    const auto track = TryMakeWidgetId(frame.m_widget.id, s_WidgetPartTrack);
    if(!track)
        return false;
    const auto thumb = TryMakeWidgetId(frame.m_widget.id, s_WidgetPartThumb);
    if(!thumb)
        return false;
    auto control = m_context.takeControlAction(frame.m_widget, true, frame.m_token);
    b8 hasControl = control.has_value();
    auto trackGesture = m_context.takePartPointerGesture(frame.m_widget, *track, true);
    b8 hasTrack = trackGesture.has_value();
    auto thumbGesture = m_context.takePartPointerGesture(frame.m_widget, *thumb, true);
    b8 hasThumb = thumbGesture.has_value();
    while(hasControl || hasTrack || hasThumb){
        if(!sliderMatches(frame))
            return false;
        if(hasControl && (!hasTrack || control->id.sequence < trackGesture->updateSequence) && (!hasThumb || control->id.sequence < thumbGesture->updateSequence)){
            if(control->control != frame.m_token || !SliderBehavior::Apply(frame.m_options, *control, frame.m_state, frame.m_result))
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            control = m_context.takeControlAction(frame.m_widget, true, frame.m_token);
            hasControl = control.has_value();
        }
        else if(hasTrack && (!hasThumb || trackGesture->updateSequence < thumbGesture->updateSequence)){
            if(trackGesture->control != frame.m_token || !SliderBehavior::Seek(frame.m_options, *trackGesture, frame.m_state, frame.m_result))
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            trackGesture = m_context.takePartPointerGesture(frame.m_widget, *track, true);
            hasTrack = trackGesture.has_value();
        }
        else{
            if(thumbGesture->control != frame.m_token || !SliderBehavior::Drag(frame.m_options, *thumbGesture, frame.m_state, frame.m_result))
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            thumbGesture = m_context.takePartPointerGesture(frame.m_widget, *thumb, true);
            hasThumb = thumbGesture.has_value();
        }
    }
    return sliderMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

