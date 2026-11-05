// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>


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
    if(!TryResolveSliderStyleMetrics(*m_skin, frame.m_options, frame.m_style, frame.m_metrics))
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


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

