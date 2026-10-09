// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box.h"
#include "../rect_math.h"

#include <global/math/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_box_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidRect(const Rect& rectangle)noexcept{
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height)
        && rectangle.width >= 0.0f && rectangle.height >= 0.0f
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
    ;
}

[[nodiscard]] static Rect Intersection(const Rect& first, const Rect& second)noexcept{
    const SIMDVector intersection = IntersectRectValue(
        VectorSet(first.x, first.y, first.width, first.height), VectorSet(second.x, second.y, second.width, second.height)
    );
    return { VectorGetX(intersection), VectorGetY(intersection), VectorGetZ(intersection), VectorGetW(intersection) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<EditBoxPlacement> EditBoxView::arrange(
    const Rect& bounds,
    const Insets& padding,
    const Rect& clip,
    const Point previousScroll,
    const f32 caretWidth,
    const bool revealCaret
)const noexcept{
    if(
        !m_ready || !__hidden_ui_edit_box_layout::ValidRect(bounds) || !__hidden_ui_edit_box_layout::ValidRect(clip)
        || !IsFinite(padding.left) || !IsFinite(padding.top) || !IsFinite(padding.right) || !IsFinite(padding.bottom)
        || padding.left < 0.0f || padding.top < 0.0f || padding.right < 0.0f || padding.bottom < 0.0f
        || !IsFinite(previousScroll.x) || previousScroll.x < 0.0f || !IsFinite(previousScroll.y) || previousScroll.y < 0.0f
        || !IsFinite(caretWidth) || caretWidth <= 0.0f
    )
        return MakeUnexpected(Failure{});
    const SIMDVector origin = VectorSet(bounds.x, bounds.y, bounds.x, bounds.y);
    const SIMDVector size = VectorSet(bounds.width, bounds.height, bounds.width, bounds.height);
    const SIMDVector paddingStart = VectorSet(padding.left, padding.top, padding.left, padding.top);
    const SIMDVector paddingEnd = VectorSet(padding.right, padding.bottom, padding.right, padding.bottom);
    const SIMDVector firstInset = VectorSelect(size, paddingStart, VectorLess(paddingStart, size));
    const SIMDVector lastInset = VectorSelect(size, paddingEnd, VectorLess(paddingEnd, size));
    const SIMDVector first = VectorAdd(origin, firstInset);
    const SIMDVector last = VectorSubtract(VectorAdd(origin, size), lastInset);
    const SIMDVector end = VectorSelect(last, first, VectorGreater(first, last));
    const SIMDVector extent = VectorSubtract(end, first);
    const Rect viewport{ VectorGetX(first), VectorGetY(first), VectorGetX(extent), VectorGetY(extent) };
    return arrangeViewport(bounds, viewport, clip, previousScroll, caretWidth, revealCaret);
}

Expected<EditBoxPlacement> EditBoxView::arrangeViewport(
    const Rect& bounds,
    const Rect& viewport,
    const Rect& clip,
    const Point previousScroll,
    const f32 caretWidth,
    const bool revealCaret
)const noexcept{
    if(
        !m_ready || !__hidden_ui_edit_box_layout::ValidRect(bounds) || !__hidden_ui_edit_box_layout::ValidRect(viewport)
        || !__hidden_ui_edit_box_layout::ValidRect(clip) || viewport.x < bounds.x || viewport.y < bounds.y
        || viewport.x + viewport.width > bounds.x + bounds.width || viewport.y + viewport.height > bounds.y + bounds.height
        || !IsFinite(previousScroll.x) || previousScroll.x < 0.0f || !IsFinite(previousScroll.y) || previousScroll.y < 0.0f
        || !IsFinite(caretWidth) || caretWidth <= 0.0f
    )
        return MakeUnexpected(Failure{});
    EditBoxPlacement placement;
    placement.bounds = bounds;
    placement.frameClip = __hidden_ui_edit_box_layout::Intersection(bounds, clip);
    placement.content = viewport;
    placement.clip = __hidden_ui_edit_box_layout::Intersection(viewport, clip);
    const f32 left = viewport.x;
    const f32 top = viewport.y;
    const auto caret = m_geometry.caretRect(m_caret);
    if(!caret)
        return MakeUnexpected(Failure{});
    const f32 maximumScroll = Max(0.0f, layout().measure().x + caretWidth - placement.content.width);
    if(!IsFinite(maximumScroll))
        return MakeUnexpected(Failure{});
    placement.scroll = Clamp(previousScroll.x, 0.0f, maximumScroll);
    if(revealCaret && caret->x < placement.scroll)
        placement.scroll = caret->x;
    if(revealCaret && caret->x + caretWidth > placement.scroll + placement.content.width)
        placement.scroll = caret->x + caretWidth - placement.content.width;
    placement.scroll = Clamp(placement.scroll, 0.0f, maximumScroll);
    if(m_textMode == EditTextMode::Multiline){
        const f32 maximumScrollY = Max(0.0f, layout().measure().y - placement.content.height);
        if(!IsFinite(maximumScrollY))
            return MakeUnexpected(Failure{});
        placement.scrollY = Clamp(previousScroll.y, 0.0f, maximumScrollY);
        if(revealCaret && caret->y < placement.scrollY)
            placement.scrollY = caret->y;
        if(revealCaret && caret->y + caret->height > placement.scrollY + placement.content.height)
            placement.scrollY = caret->y + caret->height - placement.content.height;
        placement.scrollY = Clamp(placement.scrollY, 0.0f, maximumScrollY);
        const SIMDVector origin = VectorSubtract(VectorSet(left, top, left, top),
            VectorSet(placement.scroll, placement.scrollY, placement.scroll, placement.scrollY));
        placement.textOrigin = { VectorGetX(origin), VectorGetY(origin) };
    }
    else
        placement.textOrigin = { left - placement.scroll, top + Max(0.0f, (placement.content.height - caret->height) * 0.5f) };
    const SIMDVector caretOrigin = VectorAdd(
        VectorSet(placement.textOrigin.x, placement.textOrigin.y, placement.textOrigin.x, placement.textOrigin.y),
        VectorSet(caret->x, caret->y, caret->x, caret->y)
    );
    placement.caret = { VectorGetX(caretOrigin), VectorGetY(caretOrigin), caretWidth, caret->height };
    auto selection = m_geometry.rangeOnLine(m_selection, 0u, caretWidth);
    if(!selection)
        return MakeUnexpected(Failure{});
    if(selection->width > 0.0f){
        const SIMDVector selectionOrigin = VectorAdd(VectorSet(selection->x, selection->y, selection->x, selection->y),
            VectorSet(placement.textOrigin.x, placement.textOrigin.y, placement.textOrigin.x, placement.textOrigin.y));
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
            const SIMDVector underlineOrigin = VectorAdd(VectorSet(preeditUnderline.x, preeditUnderline.y, preeditUnderline.x, preeditUnderline.y),
                VectorSet(placement.textOrigin.x, verticalOffset, placement.textOrigin.x, verticalOffset));
            preeditUnderline.x = VectorGetX(underlineOrigin);
            preeditUnderline.y = VectorGetY(underlineOrigin);
            preeditUnderline.height = thickness;
        }
    }
    if(
        !__hidden_ui_edit_box_layout::ValidRect(placement.caret) || !__hidden_ui_edit_box_layout::ValidRect(*selection)
        || !__hidden_ui_edit_box_layout::ValidRect(preeditUnderline)
        || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y)
    )
        return MakeUnexpected(Failure{});
    return placement;
}

Expected<usize> EditBoxView::hitTest(const Point point, const EditBoxPlacement& placement)const noexcept{
    if(!m_ready || !IsFinite(point.x) || !IsFinite(point.y) || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y))
        return MakeUnexpected(Failure{});
    const SIMDVector local = VectorSubtract(VectorSet(point.x, point.y, point.x, point.y),
        VectorSet(placement.textOrigin.x, placement.textOrigin.y, placement.textOrigin.x, placement.textOrigin.y));
    return m_geometry.hitTest({ VectorGetX(local), VectorGetY(local) });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

