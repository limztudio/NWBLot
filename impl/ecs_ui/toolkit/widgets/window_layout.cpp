// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "window.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WindowLayout::Measure(
    const UiSkinRegion& frame, const UiSkinRegion& title, const UiSkinRegion* collapse,
    const UiSkinRegion* resize, const WidgetStyle& style, const WindowOptions& options,
    const Point& titleSize, const f32 density, WindowMetrics& metrics
)noexcept{
    if(
        !IsFinite(style.windowCollapseExtent) || style.windowCollapseExtent <= 0.0f
        || !IsFinite(style.windowResizeExtent) || style.windowResizeExtent <= 0.0f
        || !IsFinite(style.gap) || style.gap < 0.0f || !IsFinite(density) || density <= 0.0f
        || !IsFinite(options.minimumSize.x) || options.minimumSize.x < 0.0f
        || !IsFinite(options.minimumSize.y) || options.minimumSize.y < 0.0f
    )
        return false;
    metrics.contentPadding = { frame.padding.left, frame.padding.top, frame.padding.right, frame.padding.bottom };
    metrics.titlePadding = { title.padding.left, title.padding.top, title.padding.right, title.padding.bottom };
    metrics.collapseExtent = options.collapsible ? style.windowCollapseExtent : 0.0f;
    if(options.collapsible && collapse){
        const f32 sourceExtent = static_cast<f32>(Max(collapse->rectangle.width, collapse->rectangle.height)) / density;
        metrics.collapseExtent = Max(
            metrics.collapseExtent, Max(sourceExtent, Max(collapse->minimumWidth, collapse->minimumHeight))
        );
    }
    metrics.resizeExtent = style.windowResizeExtent;
    if(resize){
        const f32 sourceExtent = static_cast<f32>(Max(resize->rectangle.width, resize->rectangle.height)) / density;
        metrics.resizeExtent = Max(metrics.resizeExtent, Max(sourceExtent, Max(resize->minimumWidth, resize->minimumHeight)));
    }
    metrics.titleHeight = Max(title.minimumHeight,
        Max(titleSize.y, metrics.collapseExtent) + metrics.titlePadding.top + metrics.titlePadding.bottom
    );
    const f32 titleWidth = titleSize.x + metrics.titlePadding.left + metrics.titlePadding.right
        + (options.collapsible ? metrics.collapseExtent + style.gap : 0.0f);
    metrics.minimumSize.x = Max(options.minimumSize.x, Max(frame.minimumWidth, Max(title.minimumWidth, titleWidth)));
    metrics.minimumSize.y = Max(options.minimumSize.y,
        Max(frame.minimumHeight, metrics.titleHeight + metrics.contentPadding.top + metrics.contentPadding.bottom)
    );
    return IsFinite(metrics.titleHeight) && IsFinite(metrics.minimumSize.x) && IsFinite(metrics.minimumSize.y)
        && metrics.titleHeight > 0.0f && metrics.minimumSize.x > 0.0f && metrics.minimumSize.y > 0.0f;
}

Rect WindowLayout::Visible(const WindowState& state, const WindowMetrics& metrics)noexcept{
    Rect bounds = state.bounds;
    if(state.collapsed)
        bounds.height = metrics.titleHeight;
    return bounds;
}

Rect WindowLayout::Content(const WindowState& state, const WindowMetrics& metrics)noexcept{
    return { state.bounds.x, state.bounds.y + metrics.titleHeight, state.bounds.width,
        Max(0.0f, state.bounds.height - metrics.titleHeight) };
}

Rect WindowLayout::Collapse(const WindowState& state, const WindowMetrics& metrics)noexcept{
    return { state.bounds.x + metrics.titlePadding.left,
        state.bounds.y + metrics.titlePadding.top
            + Max(0.0f, (metrics.titleHeight - metrics.titlePadding.top - metrics.titlePadding.bottom
                - metrics.collapseExtent) * 0.5f),
        metrics.collapseExtent, metrics.collapseExtent };
}

Rect WindowLayout::Resize(const WindowState& state, const WindowMetrics& metrics)noexcept{
    const f32 extent = Min(metrics.resizeExtent, Min(state.bounds.width, state.bounds.height - metrics.titleHeight));
    return { state.bounds.x + state.bounds.width - extent, state.bounds.y + state.bounds.height - extent, extent, extent };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

