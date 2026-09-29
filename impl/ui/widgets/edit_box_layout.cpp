// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_box_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidRect(const Rect& rectangle){
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height)
        && rectangle.width >= 0.0f && rectangle.height >= 0.0f
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
    ;
}

[[nodiscard]] static Rect Intersection(const Rect& first, const Rect& second){
    const f32 left = Max(first.x, second.x);
    const f32 top = Max(first.y, second.y);
    const f32 right = Min(first.x + first.width, second.x + second.width);
    const f32 bottom = Min(first.y + first.height, second.y + second.height);
    return { left, top, Max(0.0f, right - left), Max(0.0f, bottom - top) };
}

[[nodiscard]] static f32 ScalarFraction(StringView text, usize begin, usize end, usize position){
    usize total = 0u;
    usize before = 0u;
    for(usize index = begin; index < end; ++index){
        if(!IsUtf8Continuation(static_cast<u8>(text[index]))){
            ++total;
            if(index < position)
                ++before;
        }
    }
    return total == 0u ? 0.0f : static_cast<f32>(before) / static_cast<f32>(total);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxView::EditBoxView(Core::Alloc::GlobalArena& arena)
    : m_arena(&arena)
    , m_display(arena)
    , m_boundaries(arena)
    , m_stops(arena)
    , m_layout(arena)
{}

bool EditBoxView::snapshot(const EditModel& model){
    const AStringView committed = model.text();
    const EditCompositionView composition = model.composition();
    const usize replaced = composition.active ? composition.replacementEnd - composition.replacementStart : 0u;
    const usize preeditBytes = composition.active ? composition.text.size() : 0u;
    if(committed.size() > s_TextMaxBytes || preeditBytes > s_TextMaxBytes - (committed.size() - replaced))
        return false;
    AString<Core::Alloc::GlobalArena> display(*m_arena);
    display.reserve(committed.size() - replaced + preeditBytes);
    if(composition.active){
        display.append(committed.data(), composition.replacementStart);
        if(preeditBytes != 0u)
            display.append(composition.text.data(), preeditBytes);
        display.append(committed.data() + composition.replacementEnd, committed.size() - composition.replacementEnd);
    }
    else if(!committed.empty())
        display.assign(committed.data(), committed.size());
    EditBoundaryVector boundaries(*m_arena);
    if(!GraphemeSegmentation::Build({ display.data(), display.size() }, boundaries, true))
        return false;
    PaintVector<EditBoxCaretStop> stops(*m_arena);
    stops.reserve(model.graphemeBoundaries().size() + 1u);
    for(const usize boundary : model.graphemeBoundaries()){
        if(!composition.active || boundary <= composition.replacementStart)
            stops.push_back({ boundary, boundary, 0.0f });
        if(composition.active && boundary >= composition.replacementEnd)
            stops.push_back({ boundary, boundary - replaced + preeditBytes, 0.0f });
    }
    m_display = Move(display);
    m_boundaries = Move(boundaries);
    m_stops = Move(stops);
    m_composing = composition.active;
    m_preedit = composition.active ? EditBoxRange{ composition.replacementStart, composition.replacementStart + preeditBytes }
        : EditBoxRange{};
    m_replacement = composition.active ? EditBoxRange{ composition.replacementStart, composition.replacementEnd } : EditBoxRange{};
    m_selection = composition.active ? EditBoxRange{ m_preedit.begin + Min(composition.anchor, composition.caret),
        m_preedit.begin + Max(composition.anchor, composition.caret) }
        : EditBoxRange{ model.selectionStart(), model.selectionEnd() };
    m_caret = composition.active ? m_preedit.begin + composition.caret : model.caret();
    m_committedBytes = committed.size();
    m_revision = model.revision();
    m_ready = false;
    return true;
}

TextLayoutStatus::Enum EditBoxView::shape(TextService& text, ShapeRequest request){
    if(m_revision == 0u || request.direction != TextDirection::LeftToRight)
        return TextLayoutStatus::InvalidParameters;
    request.text = displayText();
    const TextLayoutStatus::Enum validation = ValidateTextRequest(request, false);
    if(validation != TextLayoutStatus::Success)
        return validation;
    TextLayout candidate(*m_arena);
    const TextLayoutStatus::Enum status = text.layout(request, candidate);
    if(status != TextLayoutStatus::Success)
        return status;
    return adoptLayout(Move(candidate)) ? TextLayoutStatus::Success : TextLayoutStatus::FontFailure;
}

bool EditBoxView::adoptLayout(TextLayout&& layout){
    if(m_revision == 0u || layout.utf8() != displayText() || layout.lines().size() != 1u)
        return false;
    u32 previousEnd = 0u;
    for(const TextCluster& cluster : layout.clusters()){
        if(
            cluster.lineIndex != 0u || cluster.byteBegin != previousEnd || cluster.byteEnd <= cluster.byteBegin
            || !IsFinite(cluster.leadingX) || !IsFinite(cluster.trailingX) || cluster.leadingX > cluster.trailingX
        )
            return false;
        previousEnd = cluster.byteEnd;
    }
    if(previousEnd != m_display.size())
        return false;
    PaintVector<EditBoxCaretStop> stops(*m_arena);
    stops.reserve(m_stops.size());
    for(const EditBoxCaretStop& oldStop : m_stops){
        EditBoxCaretStop stop = oldStop;
        if(!caretX(layout, stop.displayByte, stop.x))
            return false;
        stops.push_back(stop);
    }
    f32 caret = 0.0f;
    f32 anchor = 0.0f;
    if(!caretX(layout, m_caret, caret) || !caretX(layout, m_selection.begin, anchor) || !caretX(layout, m_selection.end, anchor))
        return false;
    m_layout = Move(layout);
    m_stops = Move(stops);
    m_ready = true;
    return true;
}

bool EditBoxView::caretX(const TextLayout& layout, usize byteOffset, f32& x)const{
    if(!GraphemeSegmentation::IsScalarBoundary(displayText(), byteOffset))
        return false;
    if(layout.clusters().empty()){
        if(byteOffset != 0u)
            return false;
        x = 0.0f;
        return true;
    }
    const auto cluster = LowerBound(layout.clusters().begin(), layout.clusters().end(), byteOffset,
        [](const TextCluster& item, usize value){ return item.byteEnd < value; });
    if(cluster == layout.clusters().end() || byteOffset < cluster->byteBegin)
        return false;
    if(byteOffset == cluster->byteBegin || byteOffset == cluster->byteEnd){
        x = byteOffset == cluster->byteBegin ? cluster->leadingX : cluster->trailingX;
        return true;
    }
    auto first = LowerBound(m_boundaries.begin(), m_boundaries.end(), static_cast<usize>(cluster->byteBegin));
    if(first != m_boundaries.end() && *first == cluster->byteBegin)
        ++first;
    const auto last = LowerBound(first, m_boundaries.end(), static_cast<usize>(cluster->byteEnd));
    const auto next = LowerBound(first, last, byteOffset);
    const usize intervals = static_cast<usize>(last - first) + 1u;
    f32 ordinal = static_cast<f32>(next - first);
    if(next != last && *next == byteOffset)
        ordinal += 1.0f;
    else{
        const usize begin = next == first ? cluster->byteBegin : *(next - 1);
        const usize end = next == last ? cluster->byteEnd : *next;
        ordinal += __hidden_ui_edit_box_layout::ScalarFraction(displayText(), begin, end, byteOffset);
    }
    x = cluster->leadingX + (cluster->trailingX - cluster->leadingX) * ordinal / static_cast<f32>(intervals);
    return IsFinite(x);
}

Rect EditBoxView::rangeRect(const EditBoxRange range, const Point origin)const{
    f32 begin = 0.0f;
    f32 end = 0.0f;
    if(range.begin == range.end || !caretX(m_layout, range.begin, begin) || !caretX(m_layout, range.end, end))
        return {};
    return { origin.x + Min(begin, end), origin.y, Abs(end - begin), m_layout.lines()[0].height };
}

bool EditBoxView::arrange(const Rect& bounds, const Insets& padding, const Rect& clip,
    const f32 previousScroll, EditBoxPlacement& output, const f32 caretWidth)const{
    if(
        !m_ready || !__hidden_ui_edit_box_layout::ValidRect(bounds) || !__hidden_ui_edit_box_layout::ValidRect(clip)
        || !IsFinite(padding.left) || !IsFinite(padding.top) || !IsFinite(padding.right) || !IsFinite(padding.bottom)
        || padding.left < 0.0f || padding.top < 0.0f || padding.right < 0.0f || padding.bottom < 0.0f
        || !IsFinite(previousScroll) || previousScroll < 0.0f || !IsFinite(caretWidth) || caretWidth <= 0.0f
    )
        return false;
    EditBoxPlacement placement;
    placement.bounds = bounds;
    placement.frameClip = __hidden_ui_edit_box_layout::Intersection(bounds, clip);
    const f32 left = bounds.x + Min(padding.left, bounds.width);
    const f32 top = bounds.y + Min(padding.top, bounds.height);
    const f32 right = Max(left, bounds.x + bounds.width - Min(padding.right, bounds.width));
    const f32 bottom = Max(top, bounds.y + bounds.height - Min(padding.bottom, bounds.height));
    placement.content = { left, top, right - left, bottom - top };
    placement.clip = __hidden_ui_edit_box_layout::Intersection(placement.content, clip);
    f32 caret = 0.0f;
    if(!caretX(m_layout, m_caret, caret))
        return false;
    const f32 maximumScroll = Max(0.0f, m_layout.measure().x + caretWidth - placement.content.width);
    placement.scroll = Clamp(previousScroll, 0.0f, maximumScroll);
    if(caret < placement.scroll)
        placement.scroll = caret;
    if(caret + caretWidth > placement.scroll + placement.content.width)
        placement.scroll = caret + caretWidth - placement.content.width;
    placement.scroll = Clamp(placement.scroll, 0.0f, maximumScroll);
    const TextLine& line = m_layout.lines()[0];
    placement.textOrigin = { left - placement.scroll, top + Max(0.0f, (placement.content.height - line.height) * 0.5f) };
    placement.caret = { placement.textOrigin.x + caret, placement.textOrigin.y, caretWidth, line.height };
    placement.selection = rangeRect(m_selection, placement.textOrigin);
    if(m_composing){
        placement.preeditUnderline = rangeRect(m_preedit, placement.textOrigin);
        const f32 thickness = Min(caretWidth, line.height);
        placement.preeditUnderline.y += Max(0.0f, line.height - thickness);
        placement.preeditUnderline.height = thickness;
    }
    if(
        !__hidden_ui_edit_box_layout::ValidRect(placement.caret) || !__hidden_ui_edit_box_layout::ValidRect(placement.selection)
        || !__hidden_ui_edit_box_layout::ValidRect(placement.preeditUnderline)
        || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y)
    )
        return false;
    output = placement;
    return true;
}

bool EditBoxView::hitTest(const Point point, const EditBoxPlacement& placement, usize& committedByte)const{
    if(!m_ready || m_stops.empty() || !IsFinite(point.x) || !IsFinite(point.y) || !IsFinite(placement.textOrigin.x))
        return false;
    const f32 x = point.x - placement.textOrigin.x;
    if(!IsFinite(x))
        return false;
    f32 nearest = Limit<f32>::s_Max;
    usize result = 0u;
    for(const EditBoxCaretStop& stop : m_stops){
        const f32 distance = Abs(x - stop.x);
        if(distance < nearest){
            nearest = distance;
            result = stop.committedByte;
        }
    }
    committedByte = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

