// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/bit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintSlider(const Item& item, const LayoutBox& box){
    if(item.slider >= m_scope->m_sliders.size())
        return false;
    SliderFrame& frame = *m_scope->m_sliders[item.slider];
    if(!sliderMatches(frame))
        return false;
    auto normalized = SliderBehavior::Normalize(frame.m_options.minimum, frame.m_options.maximum, frame.m_state.value());
    if(!normalized)
        return false;
    const Rect clip = visibleClip(box.clip);
    auto placement = SliderLayout::Place(box.rectangle, clip, frame.m_metrics, *normalized);
    if(!placement || !SliderBehavior::Admit(frame.m_state, frame.m_options, *placement))
        return false;
    frame.m_snapshot = frame.m_state.snapshot();
    frame.m_token = frame.m_state.controlToken();
    const bool interactive = frame.m_options.enabled && frame.m_options.minimum != frame.m_options.maximum
        && placement->centerTravel.width > 0.0f && placement->thumb.width > 0.0f && placement->thumb.height > 0.0f
        && placement->clip.width > 0.0f && placement->clip.height > 0.0f;
    InputRouter& input = m_context.input();
    const bool previouslyFocused = input.focus() == item.state.id;
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
    placement = SliderLayout::Place(box.rectangle, clip, frame.m_metrics, *normalized);
    if(!placement)
        return false;
    const WidgetId trackId = MakeWidgetId(item.state.id, "track");
    const WidgetId thumbId = MakeWidgetId(item.state.id, "thumb");
    const bool hover = interactive && input.hover() == thumbId;
    const bool pressed = interactive && input.primaryDown() && (input.capture() == trackId || input.capture() == thumbId);
    const UiSkinRegion* track = region(frame.m_style.track, frame.m_style.trackFallback);
    const UiSkinRegion* thumb = !interactive ? m_skin->findRegion(frame.m_style.disabled)
        : pressed ? m_skin->findRegion(frame.m_style.pressed) : hover ? m_skin->findRegion(frame.m_style.hover) : nullptr;
    if(!thumb)
        thumb = region(frame.m_style.normal, frame.m_style.thumbFallback);
    if(!track || !thumb)
        return false;
    const Color tint = !interactive ? frame.m_style.disabledTint : pressed ? frame.m_style.pressedTint
        : hover ? frame.m_style.hoverTint : Color{};
    m_paint.pushClip(placement->clip);
    bool painted = m_paint.drawRegion(track->name, placement->track, interactive ? Color{} : frame.m_style.disabledTint);
    if(painted)
        painted = m_paint.drawRegion(thumb->name, placement->thumb, tint);
    if(painted && interactive && input.focus() == item.state.id && m_skin->findRegion(frame.m_style.focus))
        painted = m_paint.drawRegion(frame.m_style.focus, placement->bounds);
    const bool popped = m_paint.popClip();
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
        if(!m_context.addPartTarget(item.state, trackId, trackTarget))
            return false;
        HitTarget thumbTarget;
        thumbTarget.rectangle = placement->thumb;
        thumbTarget.clip = placement->clip;
        thumbTarget.pointerGesture = true;
        thumbTarget.gestureReference = placement->travelBounds;
        thumbTarget.control = frame.m_token;
        thumbTarget.value = BitCast<u64>(frame.m_state.value());
        if(!m_context.addPartTarget(item.state, thumbId, thumbTarget))
            return false;
    }
    if(!sliderMatches(frame))
        return false;
    frame.m_state.m_placement = *placement;
    frame.m_result.valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

