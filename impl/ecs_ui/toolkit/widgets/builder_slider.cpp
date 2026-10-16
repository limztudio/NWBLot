// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_slider{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Point> MinimumSize(const UiSkinRegion& region, const f32 density)noexcept{
    if(!IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble horizontalVerticalValue = VectorDoubleDivide(
        VectorDoubleSet(static_cast<f64>(static_cast<u64>(region.sliceInsets.left) + region.sliceInsets.right), static_cast<f64>(static_cast<u64>(region.sliceInsets.top) + region.sliceInsets.bottom), 0.0, 0.0),
        VectorDoubleReplicate(density)
    );
    const SIMDVectorDouble geometryPair1Operand0 = VectorDoubleSet(static_cast<f64>(region.minimumWidth), static_cast<f64>(region.minimumHeight), 0.0, 0.0);
    const SIMDVectorDouble geometryPair1Operand1 = VectorDoubleSelect(VectorDoubleZero(), horizontalVerticalValue, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble widthHeightValue = VectorDoubleSelect(geometryPair1Operand1, geometryPair1Operand0, VectorDoubleGreater(geometryPair1Operand0, geometryPair1Operand1));
    const f64 width = VectorDoubleGetX(widthHeightValue);
    const f64 height = VectorDoubleGetY(widthHeightValue);
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    return Point{ static_cast<f32>(width), static_cast<f32>(height) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::slider(const AStringView stableKey, SliderState& state, const SliderOptions& options){
    if(declarationBlocked() || !m_scope->m_panelActive || !m_skin || m_context.failed() || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_scope->m_items.size() >= s_LayoutMaxNodes){
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
    if(!SliderBehavior::Validate(options)){
        m_context.fail();
        return false;
    }
    const auto metrics = SliderLayout::Measure(options, frame->m_style);
    if(!metrics){
        m_context.fail();
        return false;
    }
    frame->m_metrics = *metrics;
    if(!prepareSlider(*frame)){
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
    const auto admittedNode = m_scope->m_layout.addNode(m_scope->m_stack.back(), description);
    if(!admittedNode){
        m_context.fail();
        return false;
    }
    item.node = *admittedNode;
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
    const auto trackMinimum = __hidden_builder_slider::MinimumSize(*track, m_skin->referenceDensity());
    if(!trackMinimum)
        return false;
    frame.m_style.trackHeight = VectorGetY(VectorMax(VectorSet(trackMinimum->x, frame.m_style.trackHeight, 0.0f, 0.0f), VectorSet(trackMinimum->x, trackMinimum->y, 0.0f, 0.0f)));
    const UiSkinRegion* thumbs[]{ normal, m_skin->findRegion(frame.m_style.hover), m_skin->findRegion(frame.m_style.pressed), m_skin->findRegion(frame.m_style.disabled) };
    for(const UiSkinRegion* thumb : thumbs){
        if(!thumb)
            continue;
        const auto minimum = __hidden_builder_slider::MinimumSize(*thumb, m_skin->referenceDensity());
        if(!minimum)
            return false;
        const SIMDVectorSingle currentSize = VectorSet(frame.m_style.thumbExtent.x, frame.m_style.thumbExtent.y, frame.m_style.thumbExtent.x, frame.m_style.thumbExtent.y);
        const SIMDVectorSingle minimumSize = VectorSet(minimum->x, minimum->y, minimum->x, minimum->y);
        const SIMDVectorSingle maximumSize = VectorSelect(minimumSize, currentSize, VectorGreater(currentSize, minimumSize));
        frame.m_style.thumbExtent = { VectorGetX(maximumSize), VectorGetY(maximumSize) };
    }
    const auto metrics = SliderLayout::Measure(frame.m_options, frame.m_style);
    if(!metrics)
        return false;
    frame.m_metrics = *metrics;
    const SIMDVectorDouble paddedSize = VectorDoubleAdd(VectorDoubleAdd(VectorDoubleAdd(VectorDoubleSet(frame.m_metrics.padding.left, frame.m_metrics.padding.top, 0.0, 0.0), VectorDoubleSet(frame.m_metrics.padding.right, frame.m_metrics.padding.bottom, 0.0, 0.0)), VectorDoubleSet(frame.m_metrics.thumbExtent.x, frame.m_metrics.thumbExtent.y, 0.0, 0.0)), VectorDoubleSet(trackMinimum->x, 0.0, 0.0, 0.0));
    const f64 width = VectorDoubleGetX(paddedSize);
    if(!IsFinite(width) || width > Limit<f32>::s_Max)
        return false;
    const SIMDVectorSingle contentSize = VectorMax(VectorSet(frame.m_metrics.contentSize.x, frame.m_metrics.contentSize.y, 0.0f, 0.0f), VectorSet(static_cast<f32>(width), frame.m_metrics.contentSize.y, 0.0f, 0.0f));
    frame.m_metrics.contentSize = { VectorGetX(contentSize), VectorGetY(contentSize) };
    return sliderMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

