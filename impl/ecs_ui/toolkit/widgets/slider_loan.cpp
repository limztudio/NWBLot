// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::sliderMatches(const SliderFrame& frame)const{
    return popupAncestorsVisible() && frame.m_state.matches(frame.m_snapshot);
}

void Builder::publishSliderResults(const bool valid){
    const InputRouter& input = m_context.input();
    for(const auto& frame : m_scope->m_sliders){
        SliderResult result;
        if(valid && frame->m_result.valid && sliderMatches(*frame)){
            result = frame->m_result;
            result.focused = frame->m_interactive && input.focus() == frame->m_widget.id;
            const WidgetId track = MakeWidgetId(frame->m_widget.id, "track");
            const WidgetId thumb = MakeWidgetId(frame->m_widget.id, "thumb");
            result.dragging = frame->m_interactive && input.primaryDown()
                && (input.capture() == track || input.capture() == thumb);
        }
        frame->m_state.m_result = result;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

