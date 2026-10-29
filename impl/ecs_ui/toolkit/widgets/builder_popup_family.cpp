// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::popupFrameVisible(const BuilderScopeFrame& frame)const noexcept{
    return frame.m_popupState && frame.m_popupState->isOpen() && frame.m_popupState->instanceGeneration() == frame.m_popupToken.instanceGeneration && frame.m_popupState->openGeneration() == frame.m_popupToken.openGeneration;
}

b8 Builder::popupAncestorsVisible()const noexcept{
    if(m_context.failed())
        return false;
    for(const BuilderScopeFrame* frame = m_scope.get(); frame; frame = frame->m_parent){
        if(frame->m_popupState && !popupFrameVisible(*frame))
            return false;
    }
    return true;
}

b8 Builder::finishPopupFamily(){
    // Visibility is resolved without calling application sources before any deferred subtree paint.
    m_scopeFrame.m_popupVisible = popupFrameVisible(m_scopeFrame);
    if(!m_scopeFrame.m_popupVisible)
        m_context.discardPopupScope(m_scopeFrame.m_popupToken);
    for(usize index = 0u; index < m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = *m_popupFrames[index];
        frame.m_popupVisible = frame.m_parent->m_popupVisible && popupFrameVisible(frame);
        if(!frame.m_popupVisible)
            m_context.discardPopupScope(frame.m_popupToken);
    }
    const b8 painted = paintPopupFamily(m_scopeFrame);
    const b8 valid = painted && validatePopupFamily();
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        m_scope = MakeNotNull(&frame);
        publishSliderResults(valid && frame.m_popupVisible);
    }
    releasePopupFamily();
    return valid;
}

b8 Builder::paintPopupFamily(BuilderScopeFrame& frame){
    m_scope = MakeNotNull(&frame);
    if(!frame.m_popupVisible)
        return true;
    if(!popupAncestorsVisible() || !m_context.activatePopupScope(frame.m_popupToken))
        return false;
    if(!m_paint.beginOverlay(m_context.popupLayer())){
        const b8 ended = m_context.endPopupScope(false);
        if(!ended)
            m_context.fail();
        return false;
    }
    b8 painted = paintPopup() && paintDeferredContents();
    for(usize index = 0u; painted && index < m_popupFrameCount; ++index){
        BuilderScopeFrame& child = *m_popupFrames[index];
        if(child.m_parent == &frame)
            painted = paintPopupFamily(child);
    }
    m_scope = MakeNotNull(&frame);
    const b8 overlayEnded = m_paint.endOverlay();
    const b8 scopeEnded = m_context.endPopupScope(painted);
    return painted && overlayEnded && scopeEnded;
}

b8 Builder::validatePopupFamily(){
    // A child callback can change an already painted parent, or a later sibling can change an ended child.
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        m_scope = MakeNotNull(&frame);
        if(frame.m_popupVisible && (!popupFrameVisible(frame) || !validateDeferredSources()))
            return false;
    }
    // This pass calls no source code. Every model and ancestor lifetime is still borrowed until it completes.
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        m_scope = MakeNotNull(&frame);
        if(frame.m_popupVisible && (!popupFrameVisible(frame) || !validateDeferredStates()))
            return false;
    }
    return !m_context.failed();
}

void Builder::releasePopupFamily()noexcept{
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        BuilderScopeFrame& frame = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        m_scope = MakeNotNull(&frame);
        if(!frame.m_popupVisible){
            for(auto& tooltip : frame.m_tooltips){
                if(tooltip.state && tooltip.state->revision() == tooltip.revision)
                    tooltip.state->reset();
            }
        }
        releaseDeferredLoans();
        frame.m_popupState = nullptr;
    }
    m_scope = MakeNotNull(&m_scopeFrame);
}

b8 Builder::reserveCompoundPopup(const WidgetState& widget, const PopupToken& token){
    PopupOptions options;
    options.size = { 1.0f, 1.0f };
    const auto placement = PopupLayout::Place(options, m_paint.displayMetrics());
    if(!placement)
        return false;
    PopupScope scope;
    scope.token = token;
    scope.parent = m_context.popupToken();
    scope.bounds = placement->bounds;
    scope.viewport = placement->viewport;
    return m_context.registerPopupScope(widget, scope);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

