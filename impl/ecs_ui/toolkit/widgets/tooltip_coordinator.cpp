// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/tooltip_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TooltipCoordinator::declareTooltip(const AStringView stableKey, const AStringView anchorKey, const StringView text, TooltipState& state, const TooltipOptions& options){
    Item* anchor = m_builder.annotationAnchor(anchorKey);
    if(m_builder.panelDeclarationBlocked() || !anchor || m_builder.m_scope->m_tooltips.size() >= s_LayoutMaxNodes){
        m_builder.m_context.fail();
        return false;
    }
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::Tooltip);
    if(!widget || !m_builder.m_context.claimState(**widget, state.instanceGeneration()))
        return false;
    if(BindTooltipOwner((*widget)->id, (*widget)->declarationGeneration, state.m_owner, state.m_ownerDeclaration)){
        state.reset();
        state.m_owner = (*widget)->id;
        state.m_ownerDeclaration = (*widget)->declarationGeneration;
    }
    const InputRouter& input = m_builder.m_context.input();
    const HitTarget* accepted = input.findTarget(anchor->state.id, anchor->state.declarationGeneration);
    const HitTarget* hovered = input.findTarget(input.hover());
    const b8 overAnchor = hovered && (hovered->id == anchor->state.id || hovered->owner == anchor->state.id);
    const b8 eligible = options.enabled && anchor->enabled && input.windowFocused() && input.pointerKnown()
        && !m_builder.m_pointerBusy && !input.primaryDown() && !input.secondaryDown() && !input.capture().valid()
        && accepted && accepted->enabled && accepted->popup == m_builder.m_context.popupToken() && overAnchor;
    if(!TooltipBehavior::Update(state, anchor->state.id, anchor->state.declarationGeneration, m_builder.m_context.popupToken(), input.focusLossGeneration(), input.hoverActivityGeneration(), eligible, m_builder.m_deltaSeconds, options)){
        m_builder.m_context.fail();
        return false;
    }
    TooltipFrame frame(m_builder.m_arena);
    frame.widget = **widget;
    frame.anchor = anchor->state;
    frame.state = &state;
    frame.style = m_builder.m_tooltipStyle;
    frame.options = options;
    frame.revision = state.revision();
    frame.anchorIndex = static_cast<u32>(anchor - m_builder.m_scope->m_items.data());
    frame.layer = m_builder.m_context.popupLayer() + 1u;
    const ShapeRequest request = m_builder.textShapeRequest(text, m_builder.m_style.fontSize);
    auto layout = m_builder.m_text.layout(request);
    if(!layout){
        m_builder.m_context.fail();
        return false;
    }
    frame.text = Move(*layout);
    anchor->annotated = true;
    m_builder.m_scope->m_tooltips.push_back(Move(frame));
    return true;
}

b8 TooltipCoordinator::paintTooltips(){
    for(auto& frame : m_builder.m_scope->m_tooltips){
        if(!frame.state || frame.state->revision() != frame.revision || frame.anchorIndex >= m_builder.m_scope->m_items.size())
            return false;
        const Item& anchor = m_builder.m_scope->m_items[frame.anchorIndex];
        const LayoutBox* box = m_builder.m_scope->m_layout.box(anchor.node);
        if(!box || anchor.state.id != frame.anchor.id || anchor.state.declarationGeneration != frame.anchor.declarationGeneration)
            return false;
        if(!frame.state->visible())
            continue;
        const Rect clip = m_builder.visibleClip(box->clip);
        const Point& pointer = m_builder.m_context.input().pointerPosition();
        const InputRouter& input = m_builder.m_context.input();
        if(
            !anchor.enabled || !input.pointerKnown() || !input.windowFocused() || m_builder.m_pointerBusy
            || input.primaryDown() || input.secondaryDown()
            || (m_builder.m_context.topPopupToken().valid() && m_builder.m_context.topPopupToken() != frame.state->m_popup)
            || !UiRectIntersectionContainsPoint(box->rectangle, clip, pointer)
        ){
            if(!TooltipBehavior::Update(*frame.state, frame.anchor.id, frame.anchor.declarationGeneration, frame.state->m_popup, input.focusLossGeneration(), input.hoverActivityGeneration(), false, 0.0f, frame.options))
                return false;
            frame.revision = frame.state->revision();
            continue;
        }
        const UiSkinRegion* background = m_builder.region(frame.style.background, frame.style.fallback);
        if(!background)
            return false;
        const Insets padding = MaxUiPadding(frame.style.padding, background->padding);
        const auto textBounds = m_builder.m_text.measure(frame.text, m_builder.m_paint.displayMetrics());
        if(!textBounds)
            return false;
        const Point measured{ textBounds->width, textBounds->height };
        const PopupOptions options = BuildTooltipPopupOptions(box->rectangle, measured, padding, *background, frame.options);
        const auto placement = PlacePopup(options, m_builder.m_paint.displayMetrics());
        if(!placement)
            return false;
        if(!ControlVisualPainter().paintTooltipOverlay(m_builder.m_paint, m_builder.m_text, frame.layer, background->name, placement->bounds, padding, frame.text, *textBounds, frame.style.text) || frame.state->revision() != frame.revision)
            return false;
        frame.state->m_placement = *placement;
    }
    return true;
}

b8 Builder::tooltip(const AStringView stableKey, const AStringView anchorKey, const StringView text, TooltipState& state, const TooltipOptions& options){
    return TooltipCoordinator(*this).declareTooltip(stableKey, anchorKey, text, state, options);
}

b8 Builder::paintTooltips(){
    return TooltipCoordinator(*this).paintTooltips();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

