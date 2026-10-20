// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "../rect_math.h"

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_window_paint{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_ResizeGripMaximumThickness = 2.0f;
inline constexpr f32 s_ResizeGripThicknessFraction = 0.125f;
inline constexpr f32 s_ResizeGripStartFraction = 0.25f;
inline constexpr f32 s_ResizeGripLengthFraction = 0.75f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintWindow(){
    const WindowState& state = *m_scope->m_window.state;
    const WidgetStyle& style = m_scope->m_window.style;
    const Rect bounds = WindowLayout::Visible(state, m_scope->m_window.metrics);
    const Rect clip = StoreRectangle(loadVisibleClip(LoadRectangle(bounds)));
    m_paint.pushClip(clip);
    const bool painted = m_paint.drawRegion(style.window, bounds);
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
    const WidgetStyle& style = m_scope->m_window.style;
    const WindowMetrics& metrics = m_scope->m_window.metrics;
    const Rect header{ state.bounds.x, state.bounds.y, state.bounds.width, metrics.titleHeight };
    const Rect clip = StoreRectangle(loadVisibleClip(LoadRectangle(header)));
    m_paint.pushClip(clip);
    bool painted = m_paint.drawRegion(style.windowTitle, header);
    const InputRouter& input = m_context.input();
    const bool focused = input.focus() == m_scope->m_window.collapseState.id;
    if(m_scope->m_window.options.collapsible){
        const Rect collapse = WindowLayout::Collapse(state, metrics);
        const bool pressed = input.pressed(m_scope->m_window.collapseState.id);
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
    const SIMDVectorSingle collapseReservation = m_scope->m_window.options.collapsible ? VectorAdd(VectorSet(metrics.collapseExtent, 0.0f, 0.0f, 0.0f), VectorSet(style.gap, 0.0f, 0.0f, 0.0f)) : VectorZero();
    const SIMDVectorSingle padding = VectorAdd(VectorSet(metrics.titlePadding.left, metrics.titlePadding.top, metrics.titlePadding.right, metrics.titlePadding.bottom), collapseReservation);
    const SIMDVectorSingle content = InsetRectValue(VectorSet(header.x, header.y, header.width, header.height), padding);
    const SIMDVectorSingle verticalOffset = VectorMax(VectorZero(), VectorScale(VectorSubtract(VectorSet(0.0f, VectorGetW(content), 0.0f, 0.0f), VectorSet(0.0f, titleBounds.height, 0.0f, 0.0f)), s_RectCenterFraction));
    const SIMDVectorSingle textOrigin = VectorAdd(VectorSwizzle<0, 1, 0, 1>(content), verticalOffset);
    const SIMDVectorSingle alignedOrigin = VectorSubtract(textOrigin, VectorSet(titleBounds.x, titleBounds.y, titleBounds.x, titleBounds.y));
    const Point origin{ VectorGetX(alignedOrigin), VectorGetY(alignedOrigin) };
    m_paint.pushClip({ VectorGetX(content), VectorGetY(content), VectorGetZ(content), VectorGetW(content) });
    if(painted)
        painted = m_text.paint(m_paint, m_scope->m_window.title, origin, style.text);
    const bool textPopped = m_paint.popClip();
    const bool popped = m_paint.popClip();
    HitTarget title;
    title.rectangle = header;
    title.clip = clip;
    title.gestureReference = state.bounds;
    title.pointerGesture = m_scope->m_window.options.movable;
    if(!painted || !textPopped || !popped || !m_context.addTarget(m_scope->m_window.titleState, title))
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
    const WidgetStyle& style = m_scope->m_window.style;
    const Rect corner = WindowLayout::Resize(state, m_scope->m_window.metrics);
    const Rect clip = StoreRectangle(loadVisibleClip(LoadRectangle(WindowLayout::Visible(state, m_scope->m_window.metrics))));
    m_paint.pushClip(clip);
    const UiSkinRegion* resize = region(style.windowResize, style.windowResize);
    bool painted = true;
    if(resize)
        painted = m_paint.drawRegion(resize->name, corner, style.text);
    else{
        // Skins may supply a resize sprite. The default atlas uses its white sprite for a logical corner grip.
        const f32 thickness = VectorGetX(VectorMin(VectorReplicate(__hidden_ui_window_paint::s_ResizeGripMaximumThickness), VectorScale(VectorSet(corner.width, corner.height, 0.0f, 0.0f), __hidden_ui_window_paint::s_ResizeGripThicknessFraction)));
        const SIMDVectorSingle scaledSize = VectorMultiply(VectorSet(corner.width, corner.height, corner.width, corner.height), VectorSet(__hidden_ui_window_paint::s_ResizeGripStartFraction, __hidden_ui_window_paint::s_ResizeGripStartFraction, __hidden_ui_window_paint::s_ResizeGripLengthFraction, __hidden_ui_window_paint::s_ResizeGripLengthFraction));
        const SIMDVectorSingle ends = VectorMultiplyAddExpression(VectorSet(corner.width, corner.height, corner.width, corner.height), VectorSet(__hidden_ui_window_paint::s_ResizeGripStartFraction, 1.0f, 1.0f, __hidden_ui_window_paint::s_ResizeGripStartFraction), VectorSet(corner.x, corner.y, corner.x, corner.y));
        const SIMDVectorSingle origins = VectorSubtract(ends, VectorSet(0.0f, thickness, thickness, 0.0f));
        const Rect horizontal{ VectorGetX(origins), VectorGetY(origins), VectorGetZ(scaledSize), thickness };
        const Rect vertical{ VectorGetZ(origins), VectorGetW(origins), thickness, VectorGetW(scaledSize) };
        painted = m_paint.drawRegion(style.white, horizontal, style.text) && m_paint.drawRegion(style.white, vertical, style.text);
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

