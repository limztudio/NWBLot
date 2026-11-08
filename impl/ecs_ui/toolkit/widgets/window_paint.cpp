// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>


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
    const HitTarget barrier = HitTargetFactory().makeBarrierHitTarget(bounds, clip);
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
    const InputRouter& input = m_context.input();
    const b8 collapseHovered = input.hover() == m_scope->m_window.collapseState.id;
    const b8 collapsePressed = input.pressed(m_scope->m_window.collapseState.id);
    const b8 collapseFocused = input.focus() == m_scope->m_window.collapseState.id;
    const b8 painted = ControlVisualPainter().paintWindowTitleHeader(
        m_paint,
        m_text,
        *m_skin,
        state,
        style,
        metrics,
        m_scope->m_window.options.collapsible,
        collapseHovered,
        collapsePressed,
        collapseFocused,
        m_scope->m_window.title,
        m_scope->m_window.titleBounds,
        header,
        clip
    );
    const HitTarget title = HitTargetFactory().makeWindowTitleHitTarget(header, clip, state.bounds, m_scope->m_window.options.movable);
    if(!painted || !m_context.addTarget(m_scope->m_window.titleState, title))
        return false;
    if(m_scope->m_window.options.collapsible){
        const HitTarget collapse = HitTargetFactory().makeWindowCollapseHitTarget(state, metrics, clip);
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
    const b8 painted = ControlVisualPainter().paintWindowResizeCorner(m_paint, *m_skin, style, corner, clip);
    const HitTarget target = HitTargetFactory().makeWindowResizeHitTarget(corner, clip, state.bounds);
    return painted && m_context.addTarget(m_scope->m_window.resizeState, target);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

