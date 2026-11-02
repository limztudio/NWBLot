// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/paint.h>
#include <impl/ecs_ui/toolkit/widgets/window_style.adhd.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace WindowLayout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<WindowMetrics> Measure(
    const UiSkinRegion& frame, const UiSkinRegion& title, const UiSkinRegion* const collapse,
    const UiSkinRegion* const resize, const WidgetStyle& style, const WindowOptions& options,
    const Point& titleSize, const f32 density
)noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

