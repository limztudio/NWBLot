// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "selectable_paint.h"
#include "../builder.h"
#include "../layout/validation.h"
#include "../rect_math.h"

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SelectableMetrics> SelectablePainter::Measure(const UiSkin& skin, const SelectableStyle& style)noexcept{
    if(!IsBoundedUiPadding(style.padding))
        return MakeUnexpected(Failure{});
    SelectableMetrics metrics{ style.padding, {} };
    const Name states[][2]{ { style.normal, style.fallback }, { style.hover, style.hoverFallback }, { style.selected, style.selectedFallback }, { style.disabled, style.disabledFallback } };
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
        const SIMDVectorSingle currentPadding = VectorSet(metrics.padding.left, metrics.padding.top, metrics.padding.right, metrics.padding.bottom);
        const SIMDVectorSingle candidatePadding = VectorSet(padding.left, padding.top, padding.right, padding.bottom);
        const SIMDVectorSingle maximumPadding = VectorSelect(candidatePadding, currentPadding, VectorGreater(currentPadding, candidatePadding));
        metrics.padding = { VectorGetX(maximumPadding), VectorGetY(maximumPadding), VectorGetZ(maximumPadding), VectorGetW(maximumPadding) };
        const SIMDVectorSingle currentMinimum = VectorSet(metrics.minimum.x, metrics.minimum.y, 0.0f, 0.0f);
        const SIMDVectorSingle candidateMinimum = VectorSet(minimum.x, minimum.y, 0.0f, 0.0f);
        const SIMDVectorSingle maximumMinimum = VectorSelect(candidateMinimum, currentMinimum, VectorGreater(currentMinimum, candidateMinimum));
        metrics.minimum = { VectorGetX(maximumMinimum), VectorGetY(maximumMinimum) };
    }
    if(!IsBoundedUiPadding(metrics.padding))
        return MakeUnexpected(Failure{});
    return metrics;
}

bool SelectablePainter::Paint(
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
    bool painted = paint.drawRegion(region->name, bounds);
    if(painted && flags.enabled && flags.focused && skin.findRegion(style.focus))
        painted = paint.drawRegion(style.focus, bounds);
    const SIMDVectorSingle content = InsetRectValue(VectorSet(bounds.x, bounds.y, bounds.width, bounds.height), VectorSet(padding.left, padding.top, padding.right, padding.bottom));
    const f32 verticalOffset = Max(0.0f, ((VectorGetW(content) - textBounds.height) * 0.5f));
    const SIMDVectorSingle textOrigin = VectorAdd(content, VectorSet(0.0f, verticalOffset, 0.0f, 0.0f));
    const Point origin{ VectorGetX(textOrigin) - textBounds.x, VectorGetY(textOrigin) - textBounds.y };
    paint.pushClip({ VectorGetX(content), VectorGetY(content), VectorGetZ(content), VectorGetW(content) });
    if(painted)
        painted = text.paint(paint, layout, origin, flags.enabled ? textColor : disabledTextColor);
    const bool textPopped = paint.popClip();
    const bool rowPopped = paint.popClip();
    const bool clipPopped = paint.popClip();
    return painted && textPopped && rowPopped && clipPopped;
}

bool Builder::selectable(const AStringView stableKey, const StringView text, const bool selected, const WidgetOptions& options){
    Item* item = addItem(stableKey, text, WidgetKind::Selectable, options);
    if(!item)
        return false;
    item->selectableStyle = m_listStyle.row;
    item->checked = selected;
    return m_context.takeActivation(item->state, item->enabled);
}

bool Builder::paintSelectable(const Item& item, const LayoutBox& box){
    const InputRouter& input = m_context.input();
    const SelectablePaintFlags flags{ item.enabled, item.checked, input.hover() == item.state.id, input.focus() == item.state.id };
    if(!SelectablePainter::Paint(m_paint, m_text, *m_skin, item.text, item.textBounds, item.padding, box.rectangle, visibleClip(box.clip), item.selectableStyle, flags, item.style.text, item.style.disabledText))
        return false;
    HitTarget target;
    target.rectangle = box.rectangle;
    target.clip = visibleClip(box.clip);
    target.enabled = item.enabled;
    target.focusable = item.enabled;
    target.activatable = item.enabled;
    target.contextMenu = item.contextMenu;
    return m_context.addTarget(item.state, target);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

