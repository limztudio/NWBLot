// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box.h"
#include <impl/ecs_ui/toolkit/rect_math.h>


#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<EditBoxPlacement> EditBoxView::arrange(const Rect& bounds, const Insets& padding, const Rect& clip, const Point previousScroll, const f32 caretWidth, const b8 revealCaret)const noexcept{
    if(!m_ready || !IsValidUiRect(bounds) || !IsValidUiRect(clip) || !IsValidUiPadding(padding) || !IsValidUiExtent(previousScroll) || !IsFinite(caretWidth) || caretWidth <= 0.0f)
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle origin = VectorSet(bounds.x, bounds.y, bounds.x, bounds.y);
    const SIMDVectorSingle size = VectorSet(bounds.width, bounds.height, bounds.width, bounds.height);
    const SIMDVectorSingle paddingStart = VectorSet(padding.left, padding.top, padding.left, padding.top);
    const SIMDVectorSingle paddingEnd = VectorSet(padding.right, padding.bottom, padding.right, padding.bottom);
    const SIMDVectorSingle firstInset = VectorSelect(size, paddingStart, VectorLess(paddingStart, size));
    const SIMDVectorSingle lastInset = VectorSelect(size, paddingEnd, VectorLess(paddingEnd, size));
    const SIMDVectorSingle first = VectorAdd(origin, firstInset);
    const SIMDVectorSingle last = VectorSubtract(VectorAdd(origin, size), lastInset);
    const SIMDVectorSingle end = VectorSelect(last, first, VectorGreater(first, last));
    const SIMDVectorSingle extent = VectorSubtract(end, first);
    const Rect viewport{ VectorGetX(first), VectorGetY(first), VectorGetX(extent), VectorGetY(extent) };
    return arrangeViewport(bounds, viewport, clip, previousScroll, caretWidth, revealCaret);
}

Expected<EditBoxPlacement> EditBoxView::arrangeViewport(const Rect& bounds, const Rect& viewport, const Rect& clip, const Point previousScroll, const f32 caretWidth, const b8 revealCaret)const noexcept{
    if(!m_ready || !IsValidUiRect(bounds) || !IsValidUiRect(viewport) || !IsValidUiRect(clip) || viewport.x < bounds.x || viewport.y < bounds.y)
        return MakeUnexpected(Failure{});
    // Lanes are viewport right, viewport bottom, bounds right, bounds bottom. IsValidUiRect already proved these sums finite.
    const SIMDVectorSingle end = VectorAdd(VectorSet(viewport.x, viewport.y, bounds.x, bounds.y), VectorSet(viewport.width, viewport.height, bounds.width, bounds.height));
    if(VectorGetX(end) > VectorGetZ(end) || VectorGetY(end) > VectorGetW(end) || !IsValidUiExtent(previousScroll) || !IsFinite(caretWidth) || caretWidth <= 0.0f)
        return MakeUnexpected(Failure{});
    EditBoxPlacement placement;
    placement.bounds = bounds;
    const SIMDVectorSingle clipValue = LoadRect(clip);
    placement.frameClip = StoreRect(IntersectRectValue(LoadRect(bounds), clipValue));
    placement.content = viewport;
    placement.clip = StoreRect(IntersectRectValue(LoadRect(viewport), clipValue));
    const f32 left = viewport.x;
    const f32 top = viewport.y;
    const auto caret = m_geometry.caretRect(m_caret);
    if(!caret)
        return MakeUnexpected(Failure{});
    const Point logicalSize = layout().measure();
    const SIMDVectorSingle visualBoundsOrigin = VectorSet(m_visualBounds.x, m_visualBounds.y, 0.0f, 0.0f);
    const SIMDVectorSingle visualSize = VectorSet(m_visualBounds.width, m_visualBounds.height, 0.0f, 0.0f);
    const SIMDVectorSingle overhangExtent = VectorSubtract(VectorAdd(visualBoundsOrigin, visualSize), VectorSet(logicalSize.x, logicalSize.y, 0.0f, 0.0f));
    const SIMDVectorSingle overhang = VectorSelect(overhangExtent, VectorZero(), VectorGreater(VectorZero(), overhangExtent));
    const SIMDVectorSingle caretExtent = VectorSet(caretWidth, caret->height, 0.0f, 0.0f);
    const SIMDVectorSingle maximumScrolls = VectorMax(VectorZero(), VectorSubtract(VectorAdd(visualSize, VectorSet(caretWidth, 0.0f, 0.0f, 0.0f)), VectorSet(placement.content.width, placement.content.height, 0.0f, 0.0f)));
    const f32 maximumScroll = VectorGetX(maximumScrolls);
    // Scroll offsets start at the visual envelope; reveal its overhang with the logical caret at either text edge.
    const SIMDVectorSingle caretEnds = VectorAdd(VectorAdd(VectorSubtract(VectorSet(caret->x, caret->y, 0.0f, 0.0f), visualBoundsOrigin), caretExtent), overhang);
    const f32 caretEndX = VectorGetX(caretEnds);
    if(!IsFinite(maximumScroll) || !IsFinite(caretEndX))
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle previousScrollVector = VectorSet(previousScroll.x, previousScroll.y, 0.0f, 0.0f);
    const SIMDVectorSingle clampedPreviousScroll = VectorSelect(VectorZero(), VectorMin(maximumScrolls, previousScrollVector), VectorGreater(previousScrollVector, VectorZero()));
    f32 scrollX = VectorGetX(clampedPreviousScroll);
    if(revealCaret && caret->x < scrollX)
        scrollX = caret->x;
    if(revealCaret && caretEndX > scrollX + placement.content.width)
        scrollX = caretEndX - placement.content.width;
    scrollX = scrollX > 0.0f ? Min(maximumScroll, scrollX) : 0.0f;
    placement.scroll = scrollX;
    if(m_textMode == EditTextMode::Multiline){
        const f32 maximumScrollY = VectorGetY(maximumScrolls);
        const f32 caretEndY = VectorGetY(caretEnds);
        if(!IsFinite(maximumScrollY) || !IsFinite(caretEndY))
            return MakeUnexpected(Failure{});
        f32 scrollY = VectorGetY(clampedPreviousScroll);
        if(revealCaret && caret->y < scrollY)
            scrollY = caret->y;
        if(revealCaret && caretEndY > scrollY + placement.content.height)
            scrollY = caretEndY - placement.content.height;
        scrollY = scrollY > 0.0f ? Min(maximumScrollY, scrollY) : 0.0f;
        placement.scrollY = scrollY;
        const SIMDVectorSingle visualOrigin = VectorSubtract(VectorSet(left, top, left, top), VectorSet(m_visualBounds.x, m_visualBounds.y, m_visualBounds.x, m_visualBounds.y));
        const SIMDVectorSingle origin = VectorSubtract(visualOrigin, VectorSet(scrollX, scrollY, 0.0f, 0.0f));
        placement.textOrigin = { VectorGetX(origin), VectorGetY(origin) };
    }
    else{
        const f32 verticalOffset = Max(0.0f, (placement.content.height - m_visualBounds.height) * s_RectCenterFraction);
        const SIMDVectorSingle baseOrigin = VectorSubtract(VectorSet(left, top, 0.0f, 0.0f), visualBoundsOrigin);
        placement.textOrigin = { VectorGetX(baseOrigin) - placement.scroll, VectorGetY(baseOrigin) + verticalOffset };
    }
    const SIMDVectorSingle caretOrigin = VectorAdd(VectorSet(placement.textOrigin.x, placement.textOrigin.y, placement.textOrigin.x, placement.textOrigin.y), VectorSet(caret->x, caret->y, caret->x, caret->y));
    placement.caret = { VectorGetX(caretOrigin), VectorGetY(caretOrigin), caretWidth, caret->height };
    auto selection = m_geometry.rangeOnLine(m_selection, 0u, caretWidth);
    if(!selection)
        return MakeUnexpected(Failure{});
    if(selection->width > 0.0f){
        const SIMDVectorSingle selectionOrigin = VectorAdd(VectorSet(selection->x, selection->y, selection->x, selection->y), VectorSet(placement.textOrigin.x, placement.textOrigin.y, placement.textOrigin.x, placement.textOrigin.y));
        selection->x = VectorGetX(selectionOrigin);
        selection->y = VectorGetY(selectionOrigin);
    }
    Rect preeditUnderline;
    if(m_composing){
        const auto underline = m_geometry.rangeOnLine(m_preedit, 0u, caretWidth);
        if(!underline)
            return MakeUnexpected(underline.error());
        preeditUnderline = *underline;
        if(preeditUnderline.width > 0.0f){
            const f32 thickness = Min(caretWidth, preeditUnderline.height);
            const f32 verticalOffset = placement.textOrigin.y + Max(0.0f, preeditUnderline.height - thickness);
            preeditUnderline.x += placement.textOrigin.x;
            preeditUnderline.y += verticalOffset;
            preeditUnderline.height = thickness;
        }
    }
    if(!IsValidUiRect(placement.caret) || !IsValidUiRect(*selection) || !IsValidUiRect(preeditUnderline) || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y))
        return MakeUnexpected(Failure{});
    return placement;
}

Expected<usize> EditBoxView::hitTest(const Point point, const EditBoxPlacement& placement)const noexcept{
    if(!m_ready || !IsFinite(point.x) || !IsFinite(point.y) || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y))
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle local = VectorSubtract(VectorSet(point.x, point.y, point.x, point.y), VectorSet(placement.textOrigin.x, placement.textOrigin.y, placement.textOrigin.x, placement.textOrigin.y));
    return m_geometry.hitTest({ VectorGetX(local), VectorGetY(local) });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

