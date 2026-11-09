// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/ui_scope_descriptor_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>
#include <global/bit.h>
#include <impl/ecs_ui/toolkit/widgets/control_style_metrics.adhd.h>


#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::slider(const AStringView stableKey, SliderState& state, const SliderOptions& options){
    if(itemDeclarationBlocked() || !m_skin){
        m_context.fail();
        return false;
    }
    if(!synchronizePopup())
        return true;
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::Slider);
    if(!widget || !m_context.claimState(**widget, state.instanceGeneration()))
        return false;
    // Attempt diagnostics retire immediately; invalid policy leaves semantic state and accepted geometry untouched.
    state.m_result = {};
    SliderFrame frame(state);
    frame.m_options = options;
    frame.m_style = m_sliderStyle;
    frame.m_snapshot = state.snapshot();
    frame.m_widget = **widget;
    if(!IsValidSliderOptions(options)){
        m_context.fail();
        return false;
    }
    const auto metrics = MeasureSlider(options, frame.m_style);
    if(!metrics){
        m_context.fail();
        return false;
    }
    frame.m_metrics = *metrics;
    if(!prepareSlider(frame)){
        m_context.fail();
        return false;
    }
    Item item(m_arena);
    item.state = **widget;
    item.enabled = options.enabled;
    item.slider = static_cast<u32>(m_scope->m_sliders.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.intrinsicSize = frame.m_metrics.contentSize;
    if(!addItemNode(description, item))
        return false;
    m_scope->m_sliders.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    return true;
}

b8 Builder::prepareSlider(SliderFrame& frame)noexcept{
    if(!sliderMatches(frame) || !m_skin)
        return false;
    if(!ControlStyleMetrics().sliderMetrics(*m_skin, frame.m_options, frame.m_style, frame.m_metrics))
        return false;
    return sliderMatches(frame);
}

void Builder::publishSliderResults(const b8 valid)noexcept{
    const InputRouter& input = m_context.input();
    for(const auto& frame : m_scope->m_sliders){
        SliderResult result;
        if(valid && frame.m_result.valid && sliderMatches(frame)){
            const auto finalized = TryFinalizeSliderFrameResult(
                frame.m_result,
                frame.m_widget.id,
                frame.m_interactive,
                input.focus(),
                input.primaryDown(),
                input.capture()
            );
            if(!finalized){
                m_context.fail();
                frame.m_state.m_result = {};
                continue;
            }
            result = *finalized;
        }
        frame.m_state.m_result = result;
    }
}


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
    const b8 interactive = IsSliderPlacementInteractive(frame.m_options, *placement);
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
    const b8 focused = input.focus() == item.state.id;
    if(!ControlVisualPainter().paintSliderVisual(m_paint, *m_skin, frame.m_style, *placement, interactive, pressed, hover, focused) || !sliderMatches(frame))
        return false;
    const HitTarget host = HitTargetFactory().makeSliderHostHitTarget(*placement, interactive, item.contextMenu, frame.m_token, frame.m_focusOnCommit);
    if(!m_context.addTarget(item.state, host))
        return false;
    if(interactive){
        const HitTarget trackTarget = HitTargetFactory().makeSliderTrackHitTarget(*placement, frame.m_token);
        if(!m_context.addPartTarget(item.state, *trackId, trackTarget))
            return false;
        const HitTarget thumbTarget = HitTargetFactory().makeSliderThumbHitTarget(*placement, frame.m_token, frame.m_state.value());
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
        const u8 stream = SelectNextSliderInputStream(
            hasControl,
            hasControl ? control->id.sequence : 0u,
            hasTrack,
            hasTrack ? trackGesture->updateSequence : 0u,
            hasThumb,
            hasThumb ? thumbGesture->updateSequence : 0u
        );
        if(stream == 0u){
            if(control->control != frame.m_token || !SliderBehavior::Apply(frame.m_options, *control, frame.m_state, frame.m_result))
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            control = m_context.takeControlAction(frame.m_widget, true, frame.m_token);
            hasControl = control.has_value();
        }
        else if(stream == 1u){
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

