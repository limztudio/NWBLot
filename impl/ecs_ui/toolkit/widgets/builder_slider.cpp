// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_slider{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool MinimumSize(const UiSkinRegion& region, const f32 density, Point& out)noexcept{
    if(
        !IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f
        || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f
    )
        return false;
    const f64 horizontal = static_cast<f64>(static_cast<u64>(region.sliceInsets.left) + region.sliceInsets.right) / density;
    const f64 vertical = static_cast<f64>(static_cast<u64>(region.sliceInsets.top) + region.sliceInsets.bottom) / density;
    const f64 width = Max(static_cast<f64>(region.minimumWidth), horizontal);
    const f64 height = Max(static_cast<f64>(region.minimumHeight), vertical);
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return false;
    out = { static_cast<f32>(width), static_cast<f32>(height) };
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::slider(const AStringView stableKey, SliderState& state, const SliderOptions& options){
    if(
        declarationBlocked() || !m_scope->m_panelActive || !m_skin || m_context.failed()
        || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_scope->m_items.size() >= s_LayoutMaxNodes
    ){
        m_context.fail();
        return false;
    }
    if(!synchronizePopup())
        return true;
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::Slider);
    if(!widget || !m_context.claimState(*widget, state.instanceGeneration()))
        return false;
    // Attempt diagnostics retire immediately; invalid policy leaves semantic state and accepted geometry untouched.
    state.m_result = {};
    auto frame = Core::MakeGlobalUnique<SliderFrame>(m_arena, state);
    frame->m_options = options;
    frame->m_style = m_sliderStyle;
    frame->m_snapshot = state.snapshot();
    frame->m_widget = *widget;
    if(
        !SliderBehavior::Validate(options) || !SliderLayout::Measure(options, frame->m_style, frame->m_metrics)
        || !prepareSlider(*frame)
    ){
        m_context.fail();
        return false;
    }
    Item item(m_arena);
    item.state = *widget;
    item.enabled = options.enabled;
    item.slider = static_cast<u32>(m_scope->m_sliders.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.intrinsicSize = frame->m_metrics.contentSize;
    if(!m_scope->m_layout.addNode(m_scope->m_stack.back(), description, item.node)){
        m_context.fail();
        return false;
    }
    m_scope->m_sliders.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    return true;
}

bool Builder::prepareSlider(SliderFrame& frame){
    if(!sliderMatches(frame) || !m_skin || !IsFinite(m_skin->referenceDensity()) || m_skin->referenceDensity() <= 0.0f)
        return false;
    const UiSkinRegion* track = region(frame.m_style.track, frame.m_style.trackFallback);
    const UiSkinRegion* normal = region(frame.m_style.normal, frame.m_style.thumbFallback);
    if(!track || !normal)
        return false;
    Point trackMinimum;
    if(!__hidden_builder_slider::MinimumSize(*track, m_skin->referenceDensity(), trackMinimum))
        return false;
    frame.m_style.trackHeight = Max(frame.m_style.trackHeight, trackMinimum.y);
    const UiSkinRegion* thumbs[]{ normal, m_skin->findRegion(frame.m_style.hover),
        m_skin->findRegion(frame.m_style.pressed), m_skin->findRegion(frame.m_style.disabled) };
    for(const UiSkinRegion* thumb : thumbs){
        if(!thumb)
            continue;
        Point minimum;
        if(!__hidden_builder_slider::MinimumSize(*thumb, m_skin->referenceDensity(), minimum))
            return false;
        frame.m_style.thumbExtent.x = Max(frame.m_style.thumbExtent.x, minimum.x);
        frame.m_style.thumbExtent.y = Max(frame.m_style.thumbExtent.y, minimum.y);
    }
    if(!SliderLayout::Measure(frame.m_options, frame.m_style, frame.m_metrics))
        return false;
    const f64 width = static_cast<f64>(frame.m_metrics.padding.left) + frame.m_metrics.padding.right
        + frame.m_metrics.thumbExtent.x + trackMinimum.x;
    if(!IsFinite(width) || width > Limit<f32>::s_Max)
        return false;
    frame.m_metrics.contentSize.x = Max(frame.m_metrics.contentSize.x, static_cast<f32>(width));
    return sliderMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

