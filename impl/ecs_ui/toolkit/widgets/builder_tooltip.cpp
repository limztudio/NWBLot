// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::tooltip(const AStringView stableKey, const AStringView anchorKey, const StringView text,
    TooltipState& state, const TooltipOptions& options
){
    Item* anchor = annotationAnchor(anchorKey);
    if(
        declarationBlocked() || !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_context.failed()
        || !anchor || m_scope->m_tooltips.size() >= s_LayoutMaxNodes
    ){
        m_context.fail();
        return false;
    }
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::Tooltip);
    if(!widget || !m_context.claimState(*widget, state.instanceGeneration()))
        return false;
    if(state.m_owner != widget->id || state.m_ownerDeclaration != widget->declarationGeneration){
        state.reset();
        state.m_owner = widget->id;
        state.m_ownerDeclaration = widget->declarationGeneration;
    }
    const InputRouter& input = m_context.input();
    const HitTarget* accepted = input.findTarget(anchor->state.id, anchor->state.declarationGeneration);
    const HitTarget* hovered = input.findTarget(input.hover());
    const bool overAnchor = hovered && (hovered->id == anchor->state.id || hovered->owner == anchor->state.id);
    const bool eligible = options.enabled && anchor->enabled && input.windowFocused() && input.pointerKnown()
        && !m_pointerBusy && !input.primaryDown() && !input.secondaryDown() && !input.capture().valid()
        && accepted && accepted->enabled && accepted->popup == m_context.popupToken() && overAnchor;
    if(!TooltipBehavior::Update(state, anchor->state.id, anchor->state.declarationGeneration,
        m_context.popupToken(), input.focusLossGeneration(), input.hoverActivityGeneration(),
        eligible, m_deltaSeconds, options)){
        m_context.fail();
        return false;
    }
    TooltipFrame frame(m_arena);
    frame.widget = *widget;
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

bool Builder::paintTooltips(){
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
        const f32 left = Max(box->rectangle.x, clip.x);
        const f32 top = Max(box->rectangle.y, clip.y);
        const f32 right = Min(box->rectangle.x + box->rectangle.width, clip.x + clip.width);
        const f32 bottom = Min(box->rectangle.y + box->rectangle.height, clip.y + clip.height);
        const InputRouter& input = m_context.input();
        if(
            !anchor.enabled || !input.pointerKnown() || !input.windowFocused() || m_pointerBusy
            || input.primaryDown() || input.secondaryDown()
            || (m_context.topPopupToken().valid() && m_context.topPopupToken() != frame.state->m_popup)
            || pointer.x < left || pointer.y < top || pointer.x >= right || pointer.y >= bottom
        ){
            if(!TooltipBehavior::Update(*frame.state, frame.anchor.id, frame.anchor.declarationGeneration,
                frame.state->m_popup, input.focusLossGeneration(), input.hoverActivityGeneration(),
                false, 0.0f, frame.options))
                return false;
            frame.revision = frame.state->revision();
            continue;
        }
        const UiSkinRegion* background = region(frame.style.background, frame.style.fallback);
        if(!background)
            return false;
        const Insets padding{ Max(background->padding.left, frame.style.padding.left),
            Max(background->padding.top, frame.style.padding.top), Max(background->padding.right, frame.style.padding.right),
            Max(background->padding.bottom, frame.style.padding.bottom) };
        const Point measured = frame.text.measure();
        PopupOptions options;
        options.anchor = box->rectangle;
        options.size = { Min(frame.options.maximumWidth, Max(background->minimumWidth, measured.x + padding.left + padding.right)),
            Max(background->minimumHeight, measured.y + padding.top + padding.bottom) };
        options.gap = frame.options.gap;
        options.side = frame.options.side;
            const auto placement = PopupLayout::Place(options, m_paint.displayMetrics());
        if(
            !placement
            || !m_paint.beginOverlay(frame.layer)
        )
            return false;
        m_paint.pushClip(placement->bounds);
        const bool backgroundPainted = m_paint.drawRegion(background->name, placement->bounds);
        const Rect content{ placement->bounds.x + padding.left, placement->bounds.y + padding.top,
            Max(0.0f, placement->bounds.width - padding.left - padding.right),
            Max(0.0f, placement->bounds.height - padding.top - padding.bottom) };
        m_paint.pushClip(content);
        const bool painted = backgroundPainted
            && m_text.paint(m_paint, frame.text, { content.x, content.y }, frame.style.text);
        const bool contentPopped = m_paint.popClip();
        const bool popped = m_paint.popClip();
        const bool ended = m_paint.endOverlay();
        if(!painted || !contentPopped || !popped || !ended || frame.state->revision() != frame.revision)
            return false;
        frame.state->m_placement = *placement;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

