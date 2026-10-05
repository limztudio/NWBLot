// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::beginPopup(const AStringView stableKey, PopupState& state, const PopupOptions& options){
    const bool nested = m_scope->m_popupState && m_scope->m_panelActive;
    if(declarationBlocked() || (!balanced() && !nested) || !m_skin || m_context.failed()){
        m_context.fail();
        return false;
    }
    if(nested && !synchronizePopup())
        return false;
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::Popup);
    if(!widget || !m_context.claimState(*widget, state.instanceGeneration()))
        return false;
    const WidgetState declaration = *widget;
    state.bindParent(m_context.popupToken());
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
    if(!background || !PopupLayout::place(options, m_paint.displayMetrics(), placement)){
        m_context.fail();
        return false;
    }
    PopupScope scope;
    scope.token = token;
    scope.parent = m_context.popupToken();
    scope.bounds = placement.bounds;
    scope.viewport = placement.viewport;
    scope.modal = options.modal;
    scope.dismissOutside = options.dismissOutside;
    scope.dismissCancel = options.dismissCancel;
    scope.autofocus = options.autofocus;
    NotNull<BuilderScopeFrame*> next = m_scope;
    if(!nested)
        reset();
    else{
        if(m_popupFrameCount == s_InputMaxPopups - 1u){
            m_context.fail();
            return false;
        }
        if(m_popupFrameCount == m_popupFrames.size())
            m_popupFrames.push_back(Core::MakeGlobalUnique<BuilderScopeFrame>(m_arena, m_arena));
        BuilderScopeFrame& child = *m_popupFrames[m_popupFrameCount++];
        child.reset();
        child.m_parent = m_scope.get();
        next = MakeNotNull(&child);
    }
    if(!m_context.beginPopupScope(declaration, scope) || !m_context.pushScope(stableKey) || !m_paint.beginOverlay(m_context.popupLayer())){
        m_context.fail();
        return false;
    }
    m_scope = next;
    m_scope->m_panelState = declaration;
    m_scope->m_popupPaintStyle = m_popupStyle;
    m_scope->m_bounds = placement.bounds;
    LayoutNodeDesc description;
    description.direction = LayoutDirection::Column;
    description.width = { LayoutSizePolicy::Fixed, m_scope->m_bounds.width };
    description.height = { LayoutSizePolicy::Fixed, m_scope->m_bounds.height };
    description.padding = { Max(m_scope->m_popupPaintStyle.padding.left, background->padding.left),
        Max(m_scope->m_popupPaintStyle.padding.top, background->padding.top),
        Max(m_scope->m_popupPaintStyle.padding.right, background->padding.right),
        Max(m_scope->m_popupPaintStyle.padding.bottom, background->padding.bottom) };
    description.gap = m_style.gap;
    u32 node = 0u;
    if(!m_scope->m_layout.addNode(s_LayoutNoParent, description, node)){
        m_context.fail();
        return false;
    }
    m_scope->m_stack.push_back(node);
    m_scope->m_popupState = &state;
    m_scope->m_popupToken = token;
    m_scope->m_popupOptions = options;
    m_scope->m_popupPlacement = placement;
    state.m_placement = placement;
    m_scope->m_panelActive = true;
    return true;
}

bool Builder::endPopup(){
    if(declarationBlocked() || !m_scope->m_popupState || !m_scope->m_panelActive || m_scope->m_windowActive || m_scope->m_stack.size() != 1u){
        m_context.fail();
        return false;
    }
    m_finalizing = true;
    ScopeExit finish([this]()noexcept{ m_finalizing = false; });
    const bool visible = synchronizePopup();
    const bool arranged = !visible || (!m_context.failed() && m_scope->m_layout.arrange(m_scope->m_bounds));
    const bool overlayEnded = m_paint.endOverlay();
    const bool scopePopped = m_context.popScope();
    const bool popupEnded = m_context.endPopupScope(visible && arranged);
    m_scope->m_panelActive = false;
    m_scope->m_stack.clear();
    if(!arranged || !overlayEnded || !scopePopped || !popupEnded){
        m_context.fail();
        if(m_scope->m_parent)
            m_scope = MakeNotNull(m_scope->m_parent);
        else
            releasePopupFamily();
        return false;
    }
    if(m_scope->m_parent){
        m_scope = MakeNotNull(m_scope->m_parent);
        return true;
    }
    const bool finished = finishPopupFamily();
    if(!finished)
        m_context.fail();
    return finished;
}

bool Builder::synchronizePopup(){
    for(const BuilderScopeFrame* frame = m_scope.get(); frame; frame = frame->m_parent){
        if(frame->m_popupState && !popupFrameVisible(*frame)){
            m_context.input().closePopup(frame->m_popupToken);
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

