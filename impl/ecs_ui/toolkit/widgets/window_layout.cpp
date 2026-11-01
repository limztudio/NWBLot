// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "window.h"
#include <impl/ecs_ui/toolkit/images/skin_region_geometry.h>
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<WindowMetrics> WindowLayout::Measure(
    const UiSkinRegion& frame, const UiSkinRegion& title, const UiSkinRegion* collapse, const UiSkinRegion* resize, const WidgetStyle& style, const WindowOptions& options, const Point& titleSize, const f32 density)noexcept{
    if(
        !IsFinite(style.windowCollapseExtent) || style.windowCollapseExtent <= 0.0f
        || !IsFinite(style.windowResizeExtent) || style.windowResizeExtent <= 0.0f
        || !IsFinite(style.gap) || style.gap < 0.0f || !IsFinite(density) || density <= 0.0f
        || !IsValidUiExtent(options.minimumSize)
    )
        return MakeUnexpected(Failure{});
    f32 collapseExtent = options.collapsible ? style.windowCollapseExtent : 0.0f;
    if(options.collapsible && collapse)
        collapseExtent = ResolveUiSkinSquareExtent(collapseExtent, *collapse, density);
    f32 resizeExtent = style.windowResizeExtent;
    if(resize)
        resizeExtent = ResolveUiSkinSquareExtent(resizeExtent, *resize, density);
    WindowMetrics metrics;
    if(!TryMeasureWindow(frame, title, collapseExtent, resizeExtent, style, options, titleSize, density, metrics))
        return MakeUnexpected(Failure{});
    return metrics;
}

Rect WindowLayout::Content(const WindowState& state, const WindowMetrics& metrics)noexcept{
    return ResolveWindowContentBounds(state, metrics);
}

Rect WindowLayout::Collapse(const WindowState& state, const WindowMetrics& metrics)noexcept{
    return ResolveWindowCollapseBounds(state, metrics);
}

Rect WindowLayout::Resize(const WindowState& state, const WindowMetrics& metrics)noexcept{
    return ResolveWindowResizeBounds(state, metrics);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

