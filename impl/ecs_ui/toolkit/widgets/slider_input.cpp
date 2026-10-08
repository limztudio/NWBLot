// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::applySliderInput(SliderFrame& frame, const SliderPlacement& placement, const bool interactive){
    if(!sliderMatches(frame))
        return false;
    if(!interactive || placement.centerTravel.width <= 0.0f)
        return true;
    const WidgetId track = MakeWidgetId(frame.m_widget.id, "track");
    const WidgetId thumb = MakeWidgetId(frame.m_widget.id, "thumb");
    auto control = m_context.takeControlAction(frame.m_widget, true, frame.m_token);
    bool hasControl = control.has_value();
    auto trackGesture = m_context.takePartPointerGesture(frame.m_widget, track, true);
    bool hasTrack = trackGesture.has_value();
    auto thumbGesture = m_context.takePartPointerGesture(frame.m_widget, thumb, true);
    bool hasThumb = thumbGesture.has_value();
    while(hasControl || hasTrack || hasThumb){
        if(!sliderMatches(frame))
            return false;
        if(
            hasControl && (!hasTrack || control->id.sequence < trackGesture->updateSequence)
            && (!hasThumb || control->id.sequence < thumbGesture->updateSequence)
        ){
            if(
                control->control != frame.m_token
                || !SliderBehavior::Apply(frame.m_state, frame.m_options, *control, frame.m_result)
            )
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            control = m_context.takeControlAction(frame.m_widget, true, frame.m_token);
            hasControl = control.has_value();
        }
        else if(hasTrack && (!hasThumb || trackGesture->updateSequence < thumbGesture->updateSequence)){
            if(
                trackGesture->control != frame.m_token
                || !SliderBehavior::Seek(frame.m_state, frame.m_options, *trackGesture, frame.m_result)
            )
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            trackGesture = m_context.takePartPointerGesture(frame.m_widget, track, true);
            hasTrack = trackGesture.has_value();
        }
        else{
            if(
                thumbGesture->control != frame.m_token
                || !SliderBehavior::Drag(frame.m_state, frame.m_options, *thumbGesture, frame.m_result)
            )
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            thumbGesture = m_context.takePartPointerGesture(frame.m_widget, thumb, true);
            hasThumb = thumbGesture.has_value();
        }
    }
    return sliderMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

