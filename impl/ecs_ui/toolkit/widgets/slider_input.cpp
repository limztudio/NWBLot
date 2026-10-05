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
    ControlAction control;
    PointerGesture trackGesture;
    PointerGesture thumbGesture;
    bool hasControl = m_context.takeControlAction(frame.m_widget, true, frame.m_token, control);
    bool hasTrack = m_context.takePartPointerGesture(frame.m_widget, track, true, trackGesture);
    bool hasThumb = m_context.takePartPointerGesture(frame.m_widget, thumb, true, thumbGesture);
    while(hasControl || hasTrack || hasThumb){
        if(!sliderMatches(frame))
            return false;
        if(
            hasControl && (!hasTrack || control.id.sequence < trackGesture.updateSequence)
            && (!hasThumb || control.id.sequence < thumbGesture.updateSequence)
        ){
            if(
                control.control != frame.m_token
                || !SliderBehavior::apply(frame.m_state, frame.m_options, control, frame.m_result)
            )
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            hasControl = m_context.takeControlAction(frame.m_widget, true, frame.m_token, control);
        }
        else if(hasTrack && (!hasThumb || trackGesture.updateSequence < thumbGesture.updateSequence)){
            if(
                trackGesture.control != frame.m_token
                || !SliderBehavior::seek(frame.m_state, frame.m_options, trackGesture, frame.m_result)
            )
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            hasTrack = m_context.takePartPointerGesture(frame.m_widget, track, true, trackGesture);
        }
        else{
            if(
                thumbGesture.control != frame.m_token
                || !SliderBehavior::drag(frame.m_state, frame.m_options, thumbGesture, frame.m_result)
            )
                return false;
            frame.m_snapshot = frame.m_state.snapshot();
            hasThumb = m_context.takePartPointerGesture(frame.m_widget, thumb, true, thumbGesture);
        }
    }
    return sliderMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

