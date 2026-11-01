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
    const UiSkinRegion& frame, const UiSkinRegion& title, const UiSkinRegion* collapse,
    const UiSkinRegion* resize, const WidgetStyle& style, const WindowOptions& options,
    const Point& titleSize, const f32 density
)noexcept{
    if(
        !IsFinite(style.windowCollapseExtent) || style.windowCollapseExtent <= 0.0f
        || !IsFinite(style.windowResizeExtent) || style.windowResizeExtent <= 0.0f
        || !IsFinite(style.gap) || style.gap < 0.0f || !IsFinite(density) || density <= 0.0f
        || !IsValidUiExtent(options.minimumSize)
    )
        return MakeUnexpected(Failure{});
    WindowMetrics metrics;
    metrics.contentPadding = { frame.padding.left, frame.padding.top, frame.padding.right, frame.padding.bottom };
    metrics.titlePadding = { title.padding.left, title.padding.top, title.padding.right, title.padding.bottom };
    metrics.collapseExtent = options.collapsible ? style.windowCollapseExtent : 0.0f;
    if(options.collapsible && collapse)
        metrics.collapseExtent = ResolveUiSkinSquareExtent(metrics.collapseExtent, *collapse, density);
    metrics.resizeExtent = style.windowResizeExtent;
    if(resize)
        metrics.resizeExtent = ResolveUiSkinSquareExtent(metrics.resizeExtent, *resize, density);
    const SIMDVectorSingle titleContent = VectorMax(VectorSet(titleSize.x, titleSize.y, 0.0f, 0.0f), VectorSet(titleSize.x, metrics.collapseExtent, 0.0f, 0.0f));
    const SIMDVectorSingle titlePadded = VectorAdd(VectorAdd(titleContent, VectorSet(metrics.titlePadding.left, metrics.titlePadding.top, 0.0f, 0.0f)), VectorSet(metrics.titlePadding.right, metrics.titlePadding.bottom, 0.0f, 0.0f));
    metrics.titleHeight = Max(title.minimumHeight, VectorGetY(titlePadded));
    const f32 collapseWidth = options.collapsible ? metrics.collapseExtent + style.gap : 0.0f;
    const SIMDVectorSingle titleExtent = VectorSet(VectorGetX(titlePadded) + collapseWidth, metrics.titleHeight, 0.0f, 0.0f);
    const SIMDVectorSingle titleMinimum = VectorMax(VectorSet(title.minimumWidth, metrics.titleHeight, 0.0f, 0.0f), titleExtent);
    const SIMDVectorSingle contentMinimum = VectorAdd(VectorAdd(titleMinimum, VectorSet(0.0f, metrics.contentPadding.top, 0.0f, 0.0f)), VectorSet(0.0f, metrics.contentPadding.bottom, 0.0f, 0.0f));
    const SIMDVectorSingle frameMinimum = VectorSet(frame.minimumWidth, frame.minimumHeight, 0.0f, 0.0f);
    const SIMDVectorSingle minimum = VectorSelect(contentMinimum, frameMinimum, VectorGreater(frameMinimum, contentMinimum));
    const SIMDVectorSingle requested = VectorSet(options.minimumSize.x, options.minimumSize.y, 0.0f, 0.0f);
    const SIMDVectorSingle finalSize = VectorSelect(minimum, requested, VectorGreater(requested, minimum));
    metrics.minimumSize = { VectorGetX(finalSize), VectorGetY(finalSize) };
    if(!IsFinite(metrics.titleHeight) || !IsFinite(metrics.minimumSize.x) || !IsFinite(metrics.minimumSize.y) || metrics.titleHeight <= 0.0f || metrics.minimumSize.x <= 0.0f || metrics.minimumSize.y <= 0.0f)
        return MakeUnexpected(Failure{});
    return metrics;
}

Rect WindowLayout::Content(const WindowState& state, const WindowMetrics& metrics)noexcept{
    return {
        state.bounds.x,
        state.bounds.y + metrics.titleHeight,
        state.bounds.width,
        Max(0.0f, state.bounds.height - metrics.titleHeight)
    };
}

Rect WindowLayout::Collapse(const WindowState& state, const WindowMetrics& metrics)noexcept{
    const f32 x = state.bounds.x + metrics.titlePadding.left;
    const f32 availableHeight = ((metrics.titleHeight - metrics.titlePadding.top) - metrics.titlePadding.bottom) - metrics.collapseExtent;
    const f32 y = (state.bounds.y + metrics.titlePadding.top) + Max(0.0f, availableHeight * s_RectCenterFraction);
    return { x, y, metrics.collapseExtent, metrics.collapseExtent };
}

Rect WindowLayout::Resize(const WindowState& state, const WindowMetrics& metrics)noexcept{
    const f32 extent = Min(metrics.resizeExtent, Min(state.bounds.width, state.bounds.height - metrics.titleHeight));
    const SIMDVectorSingle end = VectorAdd(VectorSet(state.bounds.x, state.bounds.y, 0.0f, 0.0f), VectorSet(state.bounds.width, state.bounds.height, 0.0f, 0.0f));
    const SIMDVectorSingle origin = VectorSubtract(end, VectorReplicate(extent));
    return { VectorGetX(origin), VectorGetY(origin), extent, extent };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

