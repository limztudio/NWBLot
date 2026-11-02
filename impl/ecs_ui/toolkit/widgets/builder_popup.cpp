// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::beginPopup(const AStringView stableKey, PopupState& state, const PopupOptions& options){
    const b8 nested = m_scope->m_popupState && m_scope->m_panelActive;
    if(declarationBlocked() || (!balanced() && !nested) || !m_skin || m_context.failed()){
        m_context.fail();
        return false;
    }
    if(nested && !synchronizePopup())
        return false;
    const auto widget = m_context.declare(stableKey, WidgetKind::Popup);
    if(!widget || !m_context.claimState(**widget, state.instanceGeneration()))
        return false;
    const WidgetState declaration = **widget;
    state.bindParent(m_context.popupToken());
    const PopupToken token{ (*widget)->id, (*widget)->declarationGeneration, state.instanceGeneration(), state.openGeneration() };
    m_context.input().fencePopup(token);
    if(m_context.input().consumePopupDismissal(token))
        state.close();
    if(!state.isOpen()){
        m_context.input().closePopup(token);
        return false;
    }
    const UiSkinRegion* background = region(m_popupStyle.background, m_popupStyle.fallback);
    const auto placement = PopupLayout::Place(options, m_paint.displayMetrics());
    if(!background || !placement){
        m_context.fail();
        return false;
    }
    PopupScope scope;
    scope.token = token;
    scope.parent = m_context.popupToken();
    scope.bounds = placement->bounds;
    scope.viewport = placement->viewport;
    scope.dismissOutside = options.dismissOutside;
    scope.dismissCancel = options.dismissCancel;
    scope.autofocus = options.autofocus;
    NotNull<BuilderScopeFrame*> next = m_scope;
    if(!nested)
        reset();
    else{
        if(m_popupFrameCount >= m_popupFrames.size() || !m_popupFrames[m_popupFrameCount]){
            m_context.fail();
            return false;
        }
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
    m_scope->m_bounds = placement->bounds;
    LayoutNodeDesc description;
    description.direction = LayoutDirection::Column;
    description.width = { LayoutSizePolicy::Fixed, m_scope->m_bounds.width };
    description.height = { LayoutSizePolicy::Fixed, m_scope->m_bounds.height };
    const Insets& stylePadding = m_scope->m_popupPaintStyle.padding;
    const SIMDVectorSingle preferredPadding = VectorSet(stylePadding.left, stylePadding.top, stylePadding.right, stylePadding.bottom);
    const SIMDVectorSingle minimumPadding = VectorSet(background->padding.left, background->padding.top, background->padding.right, background->padding.bottom);
    const SIMDVectorSingle padding = VectorSelect(minimumPadding, preferredPadding, VectorGreater(preferredPadding, minimumPadding));
    description.padding = { VectorGetX(padding), VectorGetY(padding), VectorGetZ(padding), VectorGetW(padding) };
    description.gap = m_style.gap;
    u32 node = 0u;
    const auto admittedNode = m_scope->m_layout.addNode(s_LayoutNoParent, description);
    if(!admittedNode){
        m_context.fail();
        return false;
    }
    node = *admittedNode;
    m_scope->m_stack.push_back(node);
    m_scope->m_popupState = &state;
    m_scope->m_popupToken = token;
    m_scope->m_popupOptions = options;
    m_scope->m_popupPlacement = *placement;
    state.m_placement = *placement;
    m_scope->m_panelActive = true;
    return true;
}

b8 Builder::endPopup(){
    if(declarationBlocked() || !m_scope->m_popupState || !m_scope->m_panelActive || m_scope->m_windowActive || m_scope->m_stack.size() != 1u){
        m_context.fail();
        return false;
    }
    m_finalizing = true;
    ScopeExit finish([this]()noexcept{ m_finalizing = false; });
    const b8 visible = synchronizePopup();
    const b8 arranged = !visible || (!m_context.failed() && m_scope->m_layout.arrange(m_scope->m_bounds));
    const b8 overlayEnded = m_paint.endOverlay();
    const b8 scopePopped = m_context.popScope();
    const b8 popupEnded = m_context.endPopupScope(visible && arranged);
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
    const b8 finished = finishPopupFamily();
    if(!finished)
        m_context.fail();
    return finished;
}

b8 Builder::synchronizePopup()noexcept{
    for(const BuilderScopeFrame* frame = m_scope.get(); frame; frame = frame->m_parent){
        if(frame->m_popupState && !popupFrameVisible(*frame)){
            m_context.input().closePopup(frame->m_popupToken);
            return false;
        }
    }
    return true;
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

b8 Builder::paintPopup(){
    const LayoutBox* panel = m_scope->m_layout.box(0u);
    const UiSkinRegion* background = region(m_scope->m_popupPaintStyle.background, m_scope->m_popupPaintStyle.fallback);
    if(!panel || !background)
        return false;
    if(m_scope->m_popupOptions.modal){
        const Color& color = m_scope->m_popupPaintStyle.backdrop;
        if(!IsValidPaintColor(color))
            return false;
        m_paint.fillRect(m_scope->m_popupPlacement.viewport, color);
    }
    m_paint.pushClip(m_scope->m_popupPlacement.viewport);
    const b8 painted = m_paint.drawRegion(background->name, panel->rectangle);
    const b8 popped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = panel->rectangle;
    barrier.clip = m_scope->m_popupPlacement.viewport;
    if(!painted || !popped || !m_context.addTarget(m_scope->m_panelState, barrier))
        return false;
    return paintItems();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

