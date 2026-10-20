// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "window.h"
#include "../rect_math.h"

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
        || !IsFinite(options.minimumSize.x) || options.minimumSize.x < 0.0f
        || !IsFinite(options.minimumSize.y) || options.minimumSize.y < 0.0f
    )
        return MakeUnexpected(Failure{});
    WindowMetrics metrics;
    metrics.contentPadding = { frame.padding.left, frame.padding.top, frame.padding.right, frame.padding.bottom };
    metrics.titlePadding = { title.padding.left, title.padding.top, title.padding.right, title.padding.bottom };
    metrics.collapseExtent = options.collapsible ? style.windowCollapseExtent : 0.0f;
    if(options.collapsible && collapse){
        const f32 sourceExtent = VectorGetX(VectorDivide(VectorReplicate(static_cast<f32>(Max(collapse->rectangle.width, collapse->rectangle.height))), VectorReplicate(density)));
        const SIMDVectorSingle regionMinimum = VectorSet(collapse->minimumWidth, collapse->minimumHeight, 0.0f, 0.0f);
        metrics.collapseExtent = VectorGetX(VectorMax(VectorReplicate(metrics.collapseExtent), VectorMax(VectorReplicate(sourceExtent), VectorMax(regionMinimum, VectorSplatY(regionMinimum)))));
    }
    metrics.resizeExtent = style.windowResizeExtent;
    if(resize){
        const f32 sourceExtent = VectorGetX(VectorDivide(VectorReplicate(static_cast<f32>(Max(resize->rectangle.width, resize->rectangle.height))), VectorReplicate(density)));
        const SIMDVectorSingle regionMinimum = VectorSet(resize->minimumWidth, resize->minimumHeight, 0.0f, 0.0f);
        metrics.resizeExtent = VectorGetX(VectorMax(VectorReplicate(metrics.resizeExtent), VectorMax(VectorReplicate(sourceExtent), VectorMax(regionMinimum, VectorSplatY(regionMinimum)))));
    }
    const SIMDVectorSingle titleContent = VectorMax(VectorSet(titleSize.x, titleSize.y, 0.0f, 0.0f), VectorSet(titleSize.x, metrics.collapseExtent, 0.0f, 0.0f));
    const SIMDVectorSingle titlePadded = VectorAdd(VectorAdd(titleContent, VectorSet(metrics.titlePadding.left, metrics.titlePadding.top, 0.0f, 0.0f)), VectorSet(metrics.titlePadding.right, metrics.titlePadding.bottom, 0.0f, 0.0f));
    metrics.titleHeight = VectorGetY(VectorMax(VectorSet(VectorGetX(titlePadded), title.minimumHeight, 0.0f, 0.0f), titlePadded));
    const SIMDVectorSingle collapseSize = options.collapsible ? VectorAdd(VectorSet(metrics.collapseExtent, 0.0f, 0.0f, 0.0f), VectorSet(style.gap, 0.0f, 0.0f, 0.0f)) : VectorZero();
    const SIMDVectorSingle titleExtent = VectorAdd(VectorSet(VectorGetX(titlePadded), metrics.titleHeight, 0.0f, 0.0f), collapseSize);
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

Rect WindowLayout::Visible(const WindowState& state, const WindowMetrics& metrics)noexcept{
    Rect bounds = state.bounds;
    if(state.collapsed)
        bounds.height = metrics.titleHeight;
    return bounds;
}

Rect WindowLayout::Content(const WindowState& state, const WindowMetrics& metrics)noexcept{
    const SIMDVectorSingle title = VectorSet(0.0f, metrics.titleHeight, 0.0f, 0.0f);
    const SIMDVectorSingle origin = VectorAdd(VectorSet(state.bounds.x, state.bounds.y, 0.0f, 0.0f), title);
    const SIMDVectorSingle size = VectorSubtract(VectorSet(state.bounds.width, state.bounds.height, 0.0f, 0.0f), title);
    const SIMDVectorSingle boundedSize = VectorSelect(size, VectorMax(VectorZero(), size), s_SIMDMaskY);
    return { VectorGetX(origin), VectorGetY(origin), VectorGetX(boundedSize), VectorGetY(boundedSize) };
}

Rect WindowLayout::Collapse(const WindowState& state, const WindowMetrics& metrics)noexcept{
    const SIMDVectorSingle origin = VectorAdd(VectorSet(state.bounds.x, state.bounds.y, state.bounds.x, state.bounds.y), VectorSet(metrics.titlePadding.left, metrics.titlePadding.top, metrics.titlePadding.left, metrics.titlePadding.top));
    const SIMDVectorSingle available = VectorSubtract(VectorSubtract(VectorSubtract(VectorSet(0.0f, metrics.titleHeight, 0.0f, 0.0f), VectorSet(0.0f, metrics.titlePadding.top, 0.0f, 0.0f)), VectorSet(0.0f, metrics.titlePadding.bottom, 0.0f, 0.0f)), VectorSet(0.0f, metrics.collapseExtent, 0.0f, 0.0f));
    const SIMDVectorSingle centered = VectorAdd(origin, VectorMax(VectorZero(), VectorScale(available, s_RectCenterFraction)));
    return { VectorGetX(centered), VectorGetY(centered), metrics.collapseExtent, metrics.collapseExtent };
}

Rect WindowLayout::Resize(const WindowState& state, const WindowMetrics& metrics)noexcept{
    const SIMDVectorSingle contentSize = VectorSubtract(VectorSet(state.bounds.width, state.bounds.height, 0.0f, 0.0f), VectorSet(0.0f, metrics.titleHeight, 0.0f, 0.0f));
    const f32 extent = VectorGetX(VectorMin(VectorReplicate(metrics.resizeExtent), VectorMin(contentSize, VectorSplatY(contentSize))));
    const SIMDVectorSingle end = VectorAdd(VectorSet(state.bounds.x, state.bounds.y, state.bounds.x, state.bounds.y), VectorSet(state.bounds.width, state.bounds.height, state.bounds.width, state.bounds.height));
    const SIMDVectorSingle origin = VectorSubtract(end, VectorReplicate(extent));
    return { VectorGetX(origin), VectorGetY(origin), extent, extent };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

