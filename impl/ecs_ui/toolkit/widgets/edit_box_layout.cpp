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
    const f32 firstX = bounds.x + Min(padding.left, bounds.width);
    const f32 firstY = bounds.y + Min(padding.top, bounds.height);
    const f32 lastX = (bounds.x + bounds.width) - Min(padding.right, bounds.width);
    const f32 lastY = (bounds.y + bounds.height) - Min(padding.bottom, bounds.height);
    const Rect viewport{ firstX, firstY, Max(lastX, firstX) - firstX, Max(lastY, firstY) - firstY };
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
    const f32 overhangX = Max(0.0f, (m_visualBounds.x + m_visualBounds.width) - logicalSize.x);
    const f32 maximumScroll = Max(0.0f, (m_visualBounds.width + caretWidth) - placement.content.width);
    // Scroll offsets start at the visual envelope; reveal its overhang with the logical caret at either text edge.
    const f32 caretEndX = ((caret->x - m_visualBounds.x) + caretWidth) + overhangX;
    if(!IsFinite(maximumScroll) || !IsFinite(caretEndX))
        return MakeUnexpected(Failure{});
    f32 scrollX = previousScroll.x > 0.0f ? Min(maximumScroll, previousScroll.x) : 0.0f;
    if(revealCaret && caret->x < scrollX)
        scrollX = caret->x;
    if(revealCaret && caretEndX > scrollX + placement.content.width)
        scrollX = caretEndX - placement.content.width;
    scrollX = scrollX > 0.0f ? Min(maximumScroll, scrollX) : 0.0f;
    placement.scroll = scrollX;
    if(m_textMode == EditTextMode::Multiline){
        const f32 overhangY = Max(0.0f, (m_visualBounds.y + m_visualBounds.height) - logicalSize.y);
        const f32 maximumScrollY = Max(0.0f, m_visualBounds.height - placement.content.height);
        const f32 caretEndY = ((caret->y - m_visualBounds.y) + caret->height) + overhangY;
        if(!IsFinite(maximumScrollY) || !IsFinite(caretEndY))
            return MakeUnexpected(Failure{});
        f32 scrollY = previousScroll.y > 0.0f ? Min(maximumScrollY, previousScroll.y) : 0.0f;
        if(revealCaret && caret->y < scrollY)
            scrollY = caret->y;
        if(revealCaret && caretEndY > scrollY + placement.content.height)
            scrollY = caretEndY - placement.content.height;
        scrollY = scrollY > 0.0f ? Min(maximumScrollY, scrollY) : 0.0f;
        placement.scrollY = scrollY;
        placement.textOrigin = { (left - m_visualBounds.x) - scrollX, (top - m_visualBounds.y) - scrollY };
    }
    else{
        const f32 verticalOffset = Max(0.0f, (placement.content.height - m_visualBounds.height) * s_RectCenterFraction);
        placement.textOrigin = { (left - m_visualBounds.x) - placement.scroll, (top - m_visualBounds.y) + verticalOffset };
    }
    placement.caret = { placement.textOrigin.x + caret->x, placement.textOrigin.y + caret->y, caretWidth, caret->height };
    auto selection = m_geometry.rangeOnLine(m_selection, 0u, caretWidth);
    if(!selection)
        return MakeUnexpected(Failure{});
    if(selection->width > 0.0f){
        selection->x += placement.textOrigin.x;
        selection->y += placement.textOrigin.y;
    }
    Rect preeditUnderline;
    if(m_composing){
        const auto underline = m_geometry.rangeOnLine(m_preedit, 0u, caretWidth);
        if(!underline)
            return MakeUnexpected(underline.error());
        preeditUnderline = *underline;
        PlaceEditPreeditUnderline(placement.textOrigin, caretWidth, preeditUnderline);
    }
    if(!IsValidUiRect(placement.caret) || !IsValidUiRect(*selection) || !IsValidUiRect(preeditUnderline) || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y))
        return MakeUnexpected(Failure{});
    return placement;
}

Expected<usize> EditBoxView::hitTest(const Point point, const EditBoxPlacement& placement)const noexcept{
    if(!m_ready || !IsFinite(point.x) || !IsFinite(point.y) || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y))
        return MakeUnexpected(Failure{});
    return m_geometry.hitTest({ point.x - placement.textOrigin.x, point.y - placement.textOrigin.y });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

