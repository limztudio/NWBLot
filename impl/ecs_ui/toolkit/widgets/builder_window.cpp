// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::beginWindow(const AStringView stableKey, const StringView title, WindowState& state, const WindowOptions& options){
    if(declarationBlocked() || m_scope->m_panelActive || m_scope->m_windowActive || !m_skin || m_context.failed()){
        m_context.fail();
        return false;
    }
    reset();
    m_scope->m_window.style = m_style;
    const UiSkinRegion* frame = region(m_style.window, m_style.window);
    const UiSkinRegion* header = region(m_style.windowTitle, m_style.windowTitle);
    const UiSkinRegion* collapse = region(m_style.windowCollapse, m_style.windowCollapse);
    const UiSkinRegion* resize = region(m_style.windowResize, m_style.windowResize);
    const auto window = m_context.declare(stableKey, WidgetKind::Window);
    const ShapeRequest request = textShapeRequest(title, m_style.fontSize);
    if(!frame || !header || (options.collapsible && !collapse) || (options.resizable && !resize && !region(m_style.white, m_style.white)) || !window){
        m_context.fail();
        return false;
    }
    auto titleLayout = m_text.layout(request);
    if(!titleLayout){
        m_context.fail();
        return false;
    }
    m_scope->m_window.title = Move(*titleLayout);
    const auto titleBounds = m_text.measure(m_scope->m_window.title, m_paint.displayMetrics());
    if(!titleBounds){
        m_context.fail();
        return false;
    }
    m_scope->m_window.titleBounds = *titleBounds;
    const auto metrics = MeasureWindow(*frame, *header, collapse ? *collapse : UiSkinRegion{}, resize ? *resize : UiSkinRegion{}, m_style, options, { titleBounds->width, titleBounds->height }, m_skin->referenceDensity());
    if(!metrics){
        m_context.fail();
        return false;
    }
    m_scope->m_window.metrics = *metrics;
    m_scope->m_panelState = **window;
    m_scope->m_window.state = &state;
    m_scope->m_window.options = options;
    m_scope->m_window.firstUse = !state.initialized;
    WindowState candidate = state;
    if(!TryInitializeWindow(options, m_scope->m_window.metrics, candidate) || !m_context.pushScope(stableKey)){
        m_context.fail();
        return false;
    }
    // Keys beginning with @window. belong to the window's internal chrome.
    const auto titleState = m_context.declare(s_WindowChromeTitleKey, WidgetKind::Window);
    const auto collapseState = m_context.declare(s_WindowChromeCollapseKey, WidgetKind::Button);
    const auto resizeState = m_context.declare(s_WindowChromeResizeKey, WidgetKind::Window);
    if(!titleState || !collapseState || !resizeState){
        m_context.fail();
        return false;
    }
    m_scope->m_window.titleState = **titleState;
    m_scope->m_window.collapseState = **collapseState;
    m_scope->m_window.resizeState = **resizeState;
    if(!options.collapsible)
        candidate.collapsed = false;
    if(m_context.takeActivation(m_scope->m_window.collapseState, options.collapsible))
        candidate.collapsed = !candidate.collapsed;
    while(auto gesture = m_context.takePointerGesture(m_scope->m_window.titleState, options.movable)){
        if(!TryApplyWindowMove(*gesture, candidate)){
            m_context.fail();
            return false;
        }
    }
    while(auto gesture = m_context.takePointerGesture(m_scope->m_window.resizeState, options.resizable && !candidate.collapsed)){
        if(!TryApplyWindowResize(*gesture, m_scope->m_window.metrics.minimumSize, candidate)){
            m_context.fail();
            return false;
        }
    }
    if(!TryConstrainWindow(m_paint.displayMetrics(), m_scope->m_window.metrics.titleHeight, candidate)){
        m_context.fail();
        return false;
    }
    m_scope->m_bounds = ResolveWindowContentBounds(candidate, m_scope->m_window.metrics);
    LayoutNodeDesc description;
    description.direction = options.direction;
    description.width = m_scope->m_window.firstUse && options.contentWidthFirstUse ? LayoutSize{ LayoutSizePolicy::Content, 0.0f } : LayoutSize{ LayoutSizePolicy::Fixed, m_scope->m_bounds.width };
    description.height = m_scope->m_window.firstUse && options.contentHeightFirstUse ? LayoutSize{ LayoutSizePolicy::Content, 0.0f } : LayoutSize{ LayoutSizePolicy::Fixed, m_scope->m_bounds.height };
    description.padding = m_scope->m_window.metrics.contentPadding;
    description.gap = m_style.gap;
    const f32 intrinsicWidth = (m_scope->m_window.metrics.minimumSize.x - description.padding.left) - description.padding.right;
    const f32 intrinsicHeight = ((m_scope->m_window.metrics.minimumSize.y - m_scope->m_window.metrics.titleHeight) - description.padding.top) - description.padding.bottom;
    description.intrinsicSize = { Max(0.0f, intrinsicWidth), Max(0.0f, intrinsicHeight) };
    u32 node = 0u;
    const auto admittedNode = m_scope->m_layout.addNode(s_LayoutNoParent, description);
    if(!admittedNode){
        m_context.fail();
        return false;
    }
    node = *admittedNode;
    m_scope->m_stack.push_back(node);
    m_scope->m_panelActive = true;
    m_scope->m_windowActive = true;
    state = candidate;
    return !state.collapsed;
}

b8 Builder::endWindow(){
    if(declarationBlocked() || !m_scope->m_windowActive || !m_scope->m_panelActive || !m_scope->m_window.state || m_scope->m_stack.size() != 1u || m_context.failed()){
        m_context.fail();
        return false;
    }
    m_finalizing = true;
    ScopeExit finish([this]()noexcept{ m_finalizing = false; });
    WindowState& state = *m_scope->m_window.state;
    b8 arranged = true;
    if(!state.collapsed){
        const DisplayMetrics& display = m_paint.displayMetrics();
        const Rect viewport{ m_scope->m_bounds.x, m_scope->m_bounds.y, Max(display.logicalWidth, m_scope->m_bounds.width), Max(display.logicalHeight, m_scope->m_bounds.height) };
        arranged = m_scope->m_layout.arrange(viewport);
        const LayoutBox* root = m_scope->m_layout.box(0u);
        if(arranged && root){
            if(m_scope->m_window.firstUse && m_scope->m_window.options.contentWidthFirstUse)
                state.bounds.width = Max(root->rectangle.width, m_scope->m_window.metrics.minimumSize.x);
            if(m_scope->m_window.firstUse && m_scope->m_window.options.contentHeightFirstUse)
                state.bounds.height = Max(root->rectangle.height + m_scope->m_window.metrics.titleHeight, m_scope->m_window.metrics.minimumSize.y);
            arranged = TryConstrainWindow(display, m_scope->m_window.metrics.titleHeight, state);
            m_scope->m_bounds = ResolveWindowContentBounds(state, m_scope->m_window.metrics);
            if(arranged)
                arranged = m_scope->m_layout.arrange(m_scope->m_bounds);
        }
        else
            arranged = false;
    }
    const b8 painted = arranged && paintWindow();
    const b8 popped = m_context.popScope();
    m_scope->m_panelActive = false;
    m_scope->m_windowActive = false;
    m_scope->m_window.state = nullptr;
    m_scope->m_stack.clear();
    const b8 combosPainted = painted && popped && paintDeferred();
    publishSliderResults(combosPainted);
    releaseDeferredLoans();
    if(!combosPainted)
        m_context.fail();
    return combosPainted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

