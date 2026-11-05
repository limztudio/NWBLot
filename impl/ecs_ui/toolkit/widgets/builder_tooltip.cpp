// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::tooltip(const AStringView stableKey, const AStringView anchorKey, const StringView text, TooltipState& state, const TooltipOptions& options){
    Item* anchor = annotationAnchor(anchorKey);
    if(panelDeclarationBlocked() || !anchor || m_scope->m_tooltips.size() >= s_LayoutMaxNodes){
        m_context.fail();
        return false;
    }
    const auto widget = m_context.declare(stableKey, WidgetKind::Tooltip);
    if(!widget || !m_context.claimState(**widget, state.instanceGeneration()))
        return false;
    if(state.m_owner != (*widget)->id || state.m_ownerDeclaration != (*widget)->declarationGeneration){
        state.reset();
        state.m_owner = (*widget)->id;
        state.m_ownerDeclaration = (*widget)->declarationGeneration;
    }
    const InputRouter& input = m_context.input();
    const HitTarget* accepted = input.findTarget(anchor->state.id, anchor->state.declarationGeneration);
    const HitTarget* hovered = input.findTarget(input.hover());
    const b8 overAnchor = hovered && (hovered->id == anchor->state.id || hovered->owner == anchor->state.id);
    const b8 eligible = options.enabled && anchor->enabled && input.windowFocused() && input.pointerKnown()
        && !m_pointerBusy && !input.primaryDown() && !input.secondaryDown() && !input.capture().valid()
        && accepted && accepted->enabled && accepted->popup == m_context.popupToken() && overAnchor;
    if(!TooltipBehavior::Update(state, anchor->state.id, anchor->state.declarationGeneration, m_context.popupToken(), input.focusLossGeneration(), input.hoverActivityGeneration(), eligible, m_deltaSeconds, options)){
        m_context.fail();
        return false;
    }
    TooltipFrame frame(m_arena);
    frame.widget = **widget;
    frame.anchor = anchor->state;
    frame.state = &state;
    frame.style = m_tooltipStyle;
    frame.options = options;
    frame.revision = state.revision();
    frame.anchorIndex = static_cast<u32>(anchor - m_scope->m_items.data());
    frame.layer = m_context.popupLayer() + 1u;
    const ShapeRequest request = textShapeRequest(text, m_style.fontSize);
    auto layout = m_text.layout(request);
    if(!layout){
        m_context.fail();
        return false;
    }
    frame.text = Move(*layout);
    anchor->annotated = true;
    m_scope->m_tooltips.push_back(Move(frame));
    return true;
}

b8 Builder::paintTooltips(){
    for(auto& frame : m_scope->m_tooltips){
        if(!frame.state || frame.state->revision() != frame.revision || frame.anchorIndex >= m_scope->m_items.size())
            return false;
        const Item& anchor = m_scope->m_items[frame.anchorIndex];
        const LayoutBox* box = m_scope->m_layout.box(anchor.node);
        if(!box || anchor.state.id != frame.anchor.id || anchor.state.declarationGeneration != frame.anchor.declarationGeneration)
            return false;
        if(!frame.state->visible())
            continue;
        const Rect clip = visibleClip(box->clip);
        const Point& pointer = m_context.input().pointerPosition();
        const InputRouter& input = m_context.input();
        if(
            !anchor.enabled || !input.pointerKnown() || !input.windowFocused() || m_pointerBusy
            || input.primaryDown() || input.secondaryDown()
            || (m_context.topPopupToken().valid() && m_context.topPopupToken() != frame.state->m_popup)
            || !UiRectIntersectionContainsPoint(box->rectangle, clip, pointer)
        ){
            if(!TooltipBehavior::Update(*frame.state, frame.anchor.id, frame.anchor.declarationGeneration, frame.state->m_popup, input.focusLossGeneration(), input.hoverActivityGeneration(), false, 0.0f, frame.options))
                return false;
            frame.revision = frame.state->revision();
            continue;
        }
        const UiSkinRegion* background = region(frame.style.background, frame.style.fallback);
        if(!background)
            return false;
        const Insets padding = MaxUiPadding(frame.style.padding, background->padding);
        const auto textBounds = m_text.measure(frame.text, m_paint.displayMetrics());
        if(!textBounds)
            return false;
        const Point measured{ textBounds->width, textBounds->height };
        const PopupOptions options = BuildTooltipPopupOptions(box->rectangle, measured, padding, *background, frame.options);
        const auto placement = PlacePopup(options, m_paint.displayMetrics());
        if(!placement)
            return false;
        if(!PaintTooltipOverlay(m_paint, m_text, frame.layer, background->name, placement->bounds, padding, frame.text, *textBounds, frame.style.text) || frame.state->revision() != frame.revision)
            return false;
        frame.state->m_placement = *placement;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

