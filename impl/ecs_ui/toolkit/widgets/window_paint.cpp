// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintWindow(){
    const WindowState& state = *m_scope->m_window.state;
    const WidgetStyle& style = m_scope->m_window.style;
    const Rect bounds = ResolveVisibleWindowBounds(state, m_scope->m_window.metrics);
    const Rect clip = visibleClip(bounds);
    m_paint.pushClip(clip);
    const b8 painted = m_paint.drawRegion(style.window, bounds);
    const b8 popped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = bounds;
    barrier.clip = clip;
    if(!painted || !popped || !m_context.addTarget(m_scope->m_panelState, barrier) || !paintWindowTitle())
        return false;
    if(state.collapsed)
        return true;
    return paintItems() && paintWindowResize();
}

b8 Builder::paintWindowTitle(){
    const WindowState& state = *m_scope->m_window.state;
    const WidgetStyle& style = m_scope->m_window.style;
    const WindowMetrics& metrics = m_scope->m_window.metrics;
    const Rect header{ state.bounds.x, state.bounds.y, state.bounds.width, metrics.titleHeight };
    const Rect clip = visibleClip(header);
    m_paint.pushClip(clip);
    b8 painted = m_paint.drawRegion(style.windowTitle, header);
    const InputRouter& input = m_context.input();
    const b8 focused = input.focus() == m_scope->m_window.collapseState.id;
    if(m_scope->m_window.options.collapsible){
        const Rect collapse = ResolveWindowCollapseBounds(state, metrics);
        const b8 pressed = input.pressed(m_scope->m_window.collapseState.id);
        if(painted && (input.hover() == m_scope->m_window.collapseState.id || pressed)){
            const UiSkinRegion* hover = region(pressed ? style.buttonPressed : style.buttonHover, style.windowTitle);
            painted = hover && m_paint.drawRegion(hover->name, collapse);
        }
        if(painted)
            painted = m_paint.drawRegion(style.windowCollapse, collapse, state.collapsed ? style.disabledText : style.text);
        if(painted && focused && m_skin->findRegion(style.focus))
            painted = m_paint.drawRegion(style.focus, collapse);
    }
    const Rect& titleBounds = m_scope->m_window.titleBounds;
    const Rect content = ResolveWindowTitleContentBounds(header, metrics, m_scope->m_window.options.collapsible, style.gap);
    const Point origin = ResolveCenteredTextOrigin(content, 0.0f, titleBounds);
    m_paint.pushClip(content);
    if(painted)
        painted = m_text.paint(m_paint, m_scope->m_window.title, origin, style.text);
    const b8 textPopped = m_paint.popClip();
    const b8 popped = m_paint.popClip();
    HitTarget title;
    title.rectangle = header;
    title.clip = clip;
    title.gestureReference = state.bounds;
    title.pointerGesture = m_scope->m_window.options.movable;
    if(!painted || !textPopped || !popped || !m_context.addTarget(m_scope->m_window.titleState, title))
        return false;
    if(m_scope->m_window.options.collapsible){
        HitTarget collapse;
        collapse.rectangle = ResolveWindowCollapseBounds(state, metrics);
        collapse.clip = clip;
        collapse.focusable = true;
        collapse.activatable = true;
        return m_context.addTarget(m_scope->m_window.collapseState, collapse);
    }
    return true;
}

b8 Builder::paintWindowResize(){
    if(!m_scope->m_window.options.resizable)
        return true;
    const WindowState& state = *m_scope->m_window.state;
    const WidgetStyle& style = m_scope->m_window.style;
    const Rect corner = ResolveWindowResizeBounds(state, m_scope->m_window.metrics);
    const Rect clip = visibleClip(ResolveVisibleWindowBounds(state, m_scope->m_window.metrics));
    m_paint.pushClip(clip);
    const UiSkinRegion* resize = region(style.windowResize, style.windowResize);
    b8 painted = true;
    if(resize)
        painted = m_paint.drawRegion(resize->name, corner, style.text);
    else{
        // Skins may supply a resize sprite. The default atlas uses its white sprite for a logical corner grip.
        Rect horizontal;
        Rect vertical;
        ResolveWindowResizeGripBars(corner, horizontal, vertical);
        painted = m_paint.drawRegion(style.white, horizontal, style.text) && m_paint.drawRegion(style.white, vertical, style.text);
    }
    const b8 popped = m_paint.popClip();
    HitTarget target;
    target.rectangle = corner;
    target.clip = clip;
    target.gestureReference = state.bounds;
    target.pointerGesture = true;
    return painted && popped && m_context.addTarget(m_scope->m_window.resizeState, target);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
