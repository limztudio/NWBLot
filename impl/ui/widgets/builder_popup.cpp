// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::beginPopup(const AStringView stableKey, PopupState& state, const PopupOptions& options){
    if(!balanced() || !m_skin || m_context.failed()){
        m_context.fail();
        return false;
    }
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::Popup);
    if(!widget)
        return false;
    const PopupToken token{ widget->id, widget->declarationGeneration, state.instanceGeneration(), state.openGeneration() };
    m_context.input().fencePopup(token);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    if(m_context.input().consumePopupDismissal(token, reason))
        state.close();
    if(!state.isOpen()){
        m_context.input().closePopup(token);
        return false;
    }
    PopupPlacement placement;
    const UiSkinRegion* background = region(m_popupStyle.background, m_popupStyle.fallback);
    if(!background || !PopupLayout::Place(options, m_paint.displayMetrics(), placement)){
        m_context.fail();
        return false;
    }
    PopupScope scope;
    scope.token = token;
    scope.bounds = placement.bounds;
    scope.viewport = placement.viewport;
    scope.modal = options.modal;
    scope.dismissOutside = options.dismissOutside;
    scope.dismissEscape = options.dismissEscape;
    scope.autofocus = options.autofocus;
    reset();
    if(!m_context.beginPopupScope(*widget, scope) || !m_context.pushScope(stableKey) || !m_paint.beginOverlay(m_context.popupLayer())){
        m_context.fail();
        return false;
    }
    m_panelState = *widget;
    m_bounds = placement.bounds;
    LayoutNodeDesc description;
    description.direction = LayoutDirection::Column;
    description.width = { LayoutSizePolicy::Fixed, m_bounds.width };
    description.height = { LayoutSizePolicy::Fixed, m_bounds.height };
    description.padding = { Max(m_popupStyle.padding.left, background->padding.left), Max(m_popupStyle.padding.top, background->padding.top),
        Max(m_popupStyle.padding.right, background->padding.right), Max(m_popupStyle.padding.bottom, background->padding.bottom) };
    description.gap = m_style.gap;
    u32 node = 0u;
    if(!m_layout.addNode(s_LayoutNoParent, description, node)){
        m_context.fail();
        return false;
    }
    m_stack.push_back(node);
    m_popupState = &state;
    m_popupOptions = options;
    m_popupPlacement = placement;
    state.m_placement = placement;
    m_panelActive = true;
    return true;
}

bool Builder::endPopup(){
    if(!m_popupState || !m_panelActive || m_windowActive || m_stack.size() != 1u){
        m_context.fail();
        return false;
    }
    const bool visible = synchronizePopup();
    const bool painted = !visible || (!m_context.failed() && m_layout.arrange(m_bounds) && paintPopup());
    const bool overlayEnded = m_paint.endOverlay();
    const bool scopePopped = m_context.popScope();
    const bool popupEnded = m_context.endPopupScope(visible && painted);
    m_popupState = nullptr;
    m_panelActive = false;
    m_stack.clear();
    if(!painted || !overlayEnded || !scopePopped || !popupEnded)
        m_context.fail();
    return painted && overlayEnded && scopePopped && popupEnded;
}

bool Builder::synchronizePopup(){
    if(!m_popupState)
        return true;
    const PopupToken token = m_context.popupToken();
    const bool visible = m_popupState->isOpen() && m_popupState->instanceGeneration() == token.instanceGeneration
        && m_popupState->openGeneration() == token.openGeneration;
    if(!visible)
        m_context.input().closePopup(token);
    return visible;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

