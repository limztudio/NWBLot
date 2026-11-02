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
    if(!sliderMatches(frame) || !m_skin || !IsFinite(m_skin->referenceDensity()) || m_skin->referenceDensity() <= 0.0f)
        return false;
    const UiSkinRegion* track = region(frame.m_style.track, frame.m_style.trackFallback);
    const UiSkinRegion* normal = region(frame.m_style.normal, frame.m_style.thumbFallback);
    if(!track || !normal)
        return false;
    const f32 referenceDensity = m_skin->referenceDensity();
    Point trackMinimum;
    if(!TryResolveSkinRegionMinimumSize(*track, referenceDensity, trackMinimum))
        return false;
    frame.m_style.trackHeight = Max(frame.m_style.trackHeight, trackMinimum.y);
    const Array<const UiSkinRegion*, 4> thumbs{ normal, m_skin->findRegion(frame.m_style.hover), m_skin->findRegion(frame.m_style.pressed), m_skin->findRegion(frame.m_style.disabled) };
    for(const UiSkinRegion* thumb : thumbs){
        if(!thumb)
            continue;
        Point minimum;
        if(!TryResolveSkinRegionMinimumSize(*thumb, referenceDensity, minimum))
            return false;
        frame.m_style.thumbExtent = { Max(frame.m_style.thumbExtent.x, minimum.x), Max(frame.m_style.thumbExtent.y, minimum.y) };
    }
    const auto metrics = MeasureSlider(frame.m_options, frame.m_style);
    if(!metrics)
        return false;
    frame.m_metrics = *metrics;
    const f64 width = static_cast<f64>(frame.m_metrics.padding.left) + static_cast<f64>(frame.m_metrics.padding.right) + static_cast<f64>(frame.m_metrics.thumbExtent.x) + static_cast<f64>(trackMinimum.x);
    if(!IsFinite(width) || width > Limit<f32>::s_Max)
        return false;
    frame.m_metrics.contentSize.x = Max(frame.m_metrics.contentSize.x, static_cast<f32>(width));
    return sliderMatches(frame);
}

void Builder::publishSliderResults(const b8 valid)noexcept{
    const InputRouter& input = m_context.input();
    for(const auto& frame : m_scope->m_sliders){
        SliderResult result;
        if(valid && frame.m_result.valid && sliderMatches(frame)){
            result = frame.m_result;
            result.focused = frame.m_interactive && input.focus() == frame.m_widget.id;
            result.dragging = false;
            if(frame.m_interactive && input.primaryDown()){
                WidgetId track;
                WidgetId thumb;
                if(!TryMakeWidgetId(frame.m_widget.id, s_WidgetPartTrack, track) || !TryMakeWidgetId(frame.m_widget.id, s_WidgetPartThumb, thumb)){
                    m_context.fail();
                    frame.m_state.m_result = {};
                    continue;
                }
                result.dragging = input.capture() == track || input.capture() == thumb;
            }
        }
        frame.m_state.m_result = result;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

