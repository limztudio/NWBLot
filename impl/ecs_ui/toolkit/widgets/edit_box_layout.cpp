// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box.h"

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
    const f32 left = Max(first.x, second.x);
    const f32 top = Max(first.y, second.y);
    const f32 right = Min(first.x + first.width, second.x + second.width);
    const f32 bottom = Min(first.y + first.height, second.y + second.height);
    return { left, top, Max(0.0f, right - left), Max(0.0f, bottom - top) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EditBoxView::arrange(const Rect& bounds, const Insets& padding, const Rect& clip,
    const Point previousScroll, EditBoxPlacement& output, const f32 caretWidth, const bool revealCaret
)const noexcept{
    if(
        !m_ready || !__hidden_ui_edit_box_layout::ValidRect(bounds) || !__hidden_ui_edit_box_layout::ValidRect(clip)
        || !IsFinite(padding.left) || !IsFinite(padding.top) || !IsFinite(padding.right) || !IsFinite(padding.bottom)
        || padding.left < 0.0f || padding.top < 0.0f || padding.right < 0.0f || padding.bottom < 0.0f
        || !IsFinite(previousScroll.x) || previousScroll.x < 0.0f || !IsFinite(previousScroll.y) || previousScroll.y < 0.0f
        || !IsFinite(caretWidth) || caretWidth <= 0.0f
    )
        return false;
    const f32 left = bounds.x + Min(padding.left, bounds.width);
    const f32 top = bounds.y + Min(padding.top, bounds.height);
    const f32 right = Max(left, bounds.x + bounds.width - Min(padding.right, bounds.width));
    const f32 bottom = Max(top, bounds.y + bounds.height - Min(padding.bottom, bounds.height));
    return arrangeViewport(bounds, { left, top, right - left, bottom - top }, clip, previousScroll, output, caretWidth, revealCaret);
}

bool EditBoxView::arrangeViewport(const Rect& bounds, const Rect& viewport, const Rect& clip,
    const Point previousScroll, EditBoxPlacement& output, const f32 caretWidth, const bool revealCaret
)const noexcept{
    if(
        !m_ready || !__hidden_ui_edit_box_layout::ValidRect(bounds) || !__hidden_ui_edit_box_layout::ValidRect(viewport)
        || !__hidden_ui_edit_box_layout::ValidRect(clip) || viewport.x < bounds.x || viewport.y < bounds.y
        || viewport.x + viewport.width > bounds.x + bounds.width || viewport.y + viewport.height > bounds.y + bounds.height
        || !IsFinite(previousScroll.x) || previousScroll.x < 0.0f || !IsFinite(previousScroll.y) || previousScroll.y < 0.0f
        || !IsFinite(caretWidth) || caretWidth <= 0.0f
    )
        return false;
    EditBoxPlacement placement;
    placement.bounds = bounds;
    placement.frameClip = __hidden_ui_edit_box_layout::Intersection(bounds, clip);
    placement.content = viewport;
    placement.clip = __hidden_ui_edit_box_layout::Intersection(viewport, clip);
    const f32 left = viewport.x;
    const f32 top = viewport.y;
    Rect caret;
    if(!m_geometry.caretRect(m_caret, caret))
        return false;
    const f32 maximumScroll = Max(0.0f, layout().measure().x + caretWidth - placement.content.width);
    if(!IsFinite(maximumScroll))
        return false;
    placement.scroll = Clamp(previousScroll.x, 0.0f, maximumScroll);
    if(revealCaret && caret.x < placement.scroll)
        placement.scroll = caret.x;
    if(revealCaret && caret.x + caretWidth > placement.scroll + placement.content.width)
        placement.scroll = caret.x + caretWidth - placement.content.width;
    placement.scroll = Clamp(placement.scroll, 0.0f, maximumScroll);
    if(m_textMode == EditTextMode::Multiline){
        const f32 maximumScrollY = Max(0.0f, layout().measure().y - placement.content.height);
        if(!IsFinite(maximumScrollY))
            return false;
        placement.scrollY = Clamp(previousScroll.y, 0.0f, maximumScrollY);
        if(revealCaret && caret.y < placement.scrollY)
            placement.scrollY = caret.y;
        if(revealCaret && caret.y + caret.height > placement.scrollY + placement.content.height)
            placement.scrollY = caret.y + caret.height - placement.content.height;
        placement.scrollY = Clamp(placement.scrollY, 0.0f, maximumScrollY);
        placement.textOrigin = { left - placement.scroll, top - placement.scrollY };
    }
    else
        placement.textOrigin = { left - placement.scroll, top + Max(0.0f, (placement.content.height - caret.height) * 0.5f) };
    placement.caret = { placement.textOrigin.x + caret.x, placement.textOrigin.y + caret.y, caretWidth, caret.height };
    Rect selection;
    if(!m_geometry.rangeOnLine(m_selection, 0u, caretWidth, selection))
        return false;
    if(selection.width > 0.0f){
        selection.x += placement.textOrigin.x;
        selection.y += placement.textOrigin.y;
    }
    Rect preeditUnderline;
    if(m_composing){
        if(!m_geometry.rangeOnLine(m_preedit, 0u, caretWidth, preeditUnderline))
            return false;
        if(preeditUnderline.width > 0.0f){
            const f32 thickness = Min(caretWidth, preeditUnderline.height);
            preeditUnderline.x += placement.textOrigin.x;
            preeditUnderline.y += placement.textOrigin.y + Max(0.0f, preeditUnderline.height - thickness);
            preeditUnderline.height = thickness;
        }
    }
    if(
        !__hidden_ui_edit_box_layout::ValidRect(placement.caret) || !__hidden_ui_edit_box_layout::ValidRect(selection)
        || !__hidden_ui_edit_box_layout::ValidRect(preeditUnderline)
        || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y)
    )
        return false;
    output = placement;
    return true;
}

bool EditBoxView::hitTest(const Point point, const EditBoxPlacement& placement, usize& committedByte)const noexcept{
    if(!m_ready || !IsFinite(point.x) || !IsFinite(point.y) || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y))
        return false;
    return m_geometry.hitTest({ point.x - placement.textOrigin.x, point.y - placement.textOrigin.y }, committedByte);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

