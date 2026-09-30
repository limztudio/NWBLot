// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintWindow(){
    const WindowState& state = *m_scope->m_window.state;
    const Rect bounds = WindowLayout::Visible(state, m_scope->m_window.metrics);
    const Rect clip = visibleClip(bounds);
    m_paint.pushClip(clip);
    const bool painted = m_paint.drawRegion(m_style.window, bounds);
    const bool popped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = bounds;
    barrier.clip = clip;
    if(!painted || !popped || !m_context.addTarget(m_scope->m_panelState, barrier) || !paintWindowTitle())
        return false;
    if(state.collapsed)
        return true;
    return paintItems() && paintWindowResize();
}

bool Builder::paintWindowTitle(){
    const WindowState& state = *m_scope->m_window.state;
    const WindowMetrics& metrics = m_scope->m_window.metrics;
    const Rect header{ state.bounds.x, state.bounds.y, state.bounds.width, metrics.titleHeight };
    const Rect clip = visibleClip(header);
    m_paint.pushClip(clip);
    bool painted = m_paint.drawRegion(m_style.windowTitle, header);
    const InputRouter& input = m_context.input();
    const bool focused = input.focus() == m_scope->m_window.collapseState.id;
    if(m_scope->m_window.options.collapsible){
        const Rect collapse = WindowLayout::Collapse(state, metrics);
        if(painted && (input.hover() == m_scope->m_window.collapseState.id || input.capture() == m_scope->m_window.collapseState.id)){
            const bool pressed = input.capture() == m_scope->m_window.collapseState.id && input.primaryDown();
            const UiSkinRegion* hover = region(pressed ? m_style.buttonPressed : m_style.buttonHover, m_style.windowTitle);
            painted = hover && m_paint.drawRegion(hover->name, collapse);
        }
        if(painted)
            painted = m_paint.drawRegion(
                m_style.windowCollapse, collapse, state.collapsed ? m_style.disabledText : m_style.text
            );
        if(painted && focused && m_skin->findRegion(m_style.focus))
            painted = m_paint.drawRegion(m_style.focus, collapse);
    }
    const Point titleSize = m_scope->m_window.title.measure();
    Point origin{ state.bounds.x + metrics.titlePadding.left,
        state.bounds.y + metrics.titlePadding.top
            + Max(0.0f, (metrics.titleHeight - metrics.titlePadding.top - metrics.titlePadding.bottom - titleSize.y) * 0.5f) };
    if(m_scope->m_window.options.collapsible)
        origin.x += metrics.collapseExtent + m_style.gap;
    if(painted)
        painted = m_text.paint(m_paint, m_scope->m_window.title, origin, m_style.text);
    const bool popped = m_paint.popClip();
    HitTarget title;
    title.rectangle = header;
    title.clip = clip;
    title.gestureReference = state.bounds;
    title.pointerGesture = m_scope->m_window.options.movable;
    if(!painted || !popped || !m_context.addTarget(m_scope->m_window.titleState, title))
        return false;
    if(m_scope->m_window.options.collapsible){
        HitTarget collapse;
        collapse.rectangle = WindowLayout::Collapse(state, metrics);
        collapse.clip = clip;
        collapse.focusable = true;
        collapse.activatable = true;
        return m_context.addTarget(m_scope->m_window.collapseState, collapse);
    }
    return true;
}

bool Builder::paintWindowResize(){
    if(!m_scope->m_window.options.resizable)
        return true;
    const WindowState& state = *m_scope->m_window.state;
    const Rect corner = WindowLayout::Resize(state, m_scope->m_window.metrics);
    const Rect clip = visibleClip(WindowLayout::Visible(state, m_scope->m_window.metrics));
    m_paint.pushClip(clip);
    const UiSkinRegion* resize = region(m_style.windowResize, m_style.windowResize);
    bool painted = true;
    if(resize)
        painted = m_paint.drawRegion(resize->name, corner, m_style.text);
    else{
        // Skins may supply a resize sprite. The default atlas uses its white sprite for a logical corner grip.
        const f32 thickness = Min(2.0f, corner.width * 0.125f);
        const Rect horizontal{ corner.x + corner.width * 0.25f, corner.y + corner.height - thickness,
            corner.width * 0.75f, thickness };
        const Rect vertical{ corner.x + corner.width - thickness, corner.y + corner.height * 0.25f,
            thickness, corner.height * 0.75f };
        painted = m_paint.drawRegion(m_style.white, horizontal, m_style.text)
            && m_paint.drawRegion(m_style.white, vertical, m_style.text);
    }
    const bool popped = m_paint.popClip();
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

