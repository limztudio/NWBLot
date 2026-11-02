// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "selectable_paint.h"

#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SelectablePainter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SelectableMetrics> Measure(const UiSkin& skin, const SelectableStyle& style)noexcept{
    if(!IsBoundedUiPadding(style.padding))
        return MakeUnexpected(Failure{});
    SelectableMetrics metrics{ style.padding, {} };
    const Array<Array<Name, 2>, 4> states{ {
        { style.normal, style.fallback },
        { style.hover, style.hoverFallback },
        { style.selected, style.selectedFallback },
        { style.disabled, style.disabledFallback },
    } };
    for(const auto& state : states){
        const UiSkinRegion* region = skin.findRegion(state[0]);
        if(!region)
            region = skin.findRegion(state[1]);
        if(!region)
            return MakeUnexpected(Failure{});
        const Insets padding{ region->padding.left, region->padding.top, region->padding.right, region->padding.bottom };
        const Point minimum{ region->minimumWidth, region->minimumHeight };
        if(!IsBoundedUiPadding(padding) || !IsValidUiExtent(minimum))
            return MakeUnexpected(Failure{});
        metrics.padding = MaxUiPadding(metrics.padding, padding);
        metrics.minimum = { Max(metrics.minimum.x, minimum.x), Max(metrics.minimum.y, minimum.y) };
    }
    if(!IsBoundedUiPadding(metrics.padding))
        return MakeUnexpected(Failure{});
    return metrics;
}

b8 Paint(
    PaintBuilder& paint, TextService& text, const UiSkin& skin, const TextLayout& layout,
    const Rect& textBounds, const Insets& padding, const Rect& bounds, const Rect& clip, const SelectableStyle& style,
    const SelectablePaintFlags& flags, const Color& textColor, const Color& disabledTextColor
){
    const Name& preferred = !flags.enabled ? style.disabled : flags.selected ? style.selected : flags.hover ? style.hover : style.normal;
    const Name& fallback = !flags.enabled ? style.disabledFallback : flags.selected ? style.selectedFallback : flags.hover ? style.hoverFallback : style.fallback;
    const UiSkinRegion* region = skin.findRegion(preferred);
    if(!region)
        region = skin.findRegion(fallback);
    if(!region)
        return false;
    paint.pushClip(clip);
    paint.pushClip(bounds);
    b8 painted = paint.drawRegion(region->name, bounds);
    if(painted && flags.enabled && flags.focused && skin.findRegion(style.focus))
        painted = paint.drawRegion(style.focus, bounds);
    const Rect content = StoreRect(InsetRectValue(LoadRect(bounds), VectorSet(padding.left, padding.top, padding.right, padding.bottom)));
    const Point origin = ResolveCenteredTextOrigin(content, 0.0f, textBounds);
    paint.pushClip(content);
    if(painted)
        painted = text.paint(paint, layout, origin, flags.enabled ? textColor : disabledTextColor);
    const b8 textPopped = paint.popClip();
    const b8 rowPopped = paint.popClip();
    const b8 clipPopped = paint.popClip();
    return painted && textPopped && rowPopped && clipPopped;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

